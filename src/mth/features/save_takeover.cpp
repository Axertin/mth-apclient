#include "mth/features/save_takeover.hpp"

#include "mod/mod_api.hpp"
#include "mth/core/data/game_layout.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_mem.hpp"

namespace mth
{
namespace
{
// The vanilla slot staged into. Arbitrary once writes are suppressed; fixed so the takeover is
// deterministic. Its on-disk contents are never modified.
constexpr unsigned int kScratchSlot = 0;

// The menu pointer is normalized by the PAL detour (Windows hands the detour a base subobject), so
// the shared mth::layout offsets apply on both platforms from here down.
int *menu_field(void *menu, std::ptrdiff_t off)
{
    return reinterpret_cast<int *>(static_cast<char *>(menu) + off);
}

// A menu parked outside the range its own dispatch tables accept is either mid-construction or a
// mis-based pointer; writing through the latter corrupts whatever the offsets land on.
bool menu_drivable(void *menu)
{
    if (!pal::pointer_looks_valid(menu))
        return false;
    const int state = *menu_field(menu, layout::kProfileMenuStateOff);
    if (state >= layout::kProfileMenuStateMin && state <= layout::kProfileMenuStateMax)
        return true;
    static bool warned = false;
    if (!warned)
    {
        warned = true;
        pal::logf(pal::LogLevel::Warn, "takeover: profile menu %p state %d outside [%d,%d]; not driving it", menu, state, layout::kProfileMenuStateMin,
                  layout::kProfileMenuStateMax);
    }
    return false;
}

int menu_state(void *menu)
{
    return menu_drivable(menu) ? *menu_field(menu, layout::kProfileMenuStateOff) : -1;
}

// Requests the state rather than writing it, so the menu's own state machine still runs its
// enter/exit. Idempotent: re-requesting a state already pending would restart it.
bool menu_request_state(void *menu, int state)
{
    if (!menu_drivable(menu))
        return false;
    int *current = menu_field(menu, layout::kProfileMenuStateOff);
    int *next = menu_field(menu, layout::kProfileMenuNextStateOff);
    if (*current == state || *next == state)
        return true;
    *next = state;
    *reinterpret_cast<unsigned char *>(static_cast<char *>(menu) + layout::kProfileMenuStateReqOff) = 1;
    return true;
}
} // namespace

SaveTakeover::SaveTakeover(ApSaveStore store, IdentityFn identity) : store_(std::move(store)), identity_(std::move(identity))
{
    if (!pal::install_save_request_hook([this] { on_game_save_requested(); }))
        pal::logf(pal::LogLevel::Warn, "takeover: save-request hook unavailable; mod saves will not flush");
    menu_hook_ok_ = pal::install_profile_menu_hook([this](void *menu) { on_profile_menu(menu); }); // logs its own failure
}

SaveTakeover::~SaveTakeover()
{
    pal::remove_profile_menu_hook();
    pal::remove_save_request_hook();
}

bool SaveTakeover::begin()
{
    // Without the menu hook nothing would ever drive the launch, and the recovery that bounces the
    // player back to the title lives in that same callback. Refuse the launch instead.
    if (!menu_hook_ok_)
    {
        pal::logf(pal::LogLevel::Error, "takeover: profile-menu hook never installed; refusing to start");
        return false;
    }

    // Running means the player came back to the title from a live session (flush first so progress is
    // not dropped); Failed means the prior attempt already gave up. Both re-arm. Every other non-Idle
    // step is mid-sequence.
    if (step_ == TakeoverStep::Running)
        flush();
    else if (!takeover_settled(step_))
        return true; // already claimed, mid-flight

    auto [seed, slot] = identity_();
    seed_ = std::move(seed);
    slot_ = std::move(slot);

    mod::set_save_write_enabled(false); // re-assert; off since connect, this covers a savetest re-enable
    step_ = TakeoverStep::AwaitingMenu;
    frames_in_step_ = 0;
    pal::logf(pal::LogLevel::Info, "takeover: begin seed=%s slot=%s", seed_.c_str(), slot_.c_str());
    return true;
}

void SaveTakeover::set_step(TakeoverStep next)
{
    pal::logf(pal::LogLevel::Info, "takeover: %s -> %s", takeover_step_name(step_), takeover_step_name(next));
    step_ = next;
    frames_in_step_ = 0;
}

void SaveTakeover::tick()
{
    if (takeover_settled(step_))
        return; // begin() re-arms these; the flush is driven by the save-request hook, not the tick

    TakeoverInputs in;
    in.save_api_ready = mod::save_api_available();
    const int gs = mod::current_game_state();
    // -1 = accessor unavailable, must not read as "entered". Profile-select is its own gamestate, so
    // excluding only the title screen would read the menu opening as gameplay starting.
    in.gameplay_entered = gs >= 0 && gs != layout::kTitleGameState && gs != layout::kProfileSelectGameState;
    in.frames_in_step = frames_in_step_;

    const TakeoverStep next = next_takeover_step(step_, in);
    if (next == step_)
    {
        ++frames_in_step_;
        return;
    }
    if (next == TakeoverStep::Failed)
    {
        fail("the sequence stalled or the save api dropped");
        return;
    }
    set_step(next);
}

void SaveTakeover::fail(const char *why)
{
    // Writes stay disabled: a half-taken-over session must not persist anything. The profile-menu
    // callback returns the player to the title from here.
    set_step(TakeoverStep::Failed);
    pal::logf(pal::LogLevel::Error, "takeover: aborted (%s); save writes remain disabled", why);
}

void SaveTakeover::on_profile_menu(void *menu)
{
    // A takeover that gave up must not leave the player on a live profile-select, where they could
    // start a vanilla save inside a mod session.
    if (step_ == TakeoverStep::Failed)
    {
        menu_request_state(menu, layout::kProfileMenuBackState);
        return;
    }
    if (step_ != TakeoverStep::AwaitingMenu)
        return;
    // Browse state only. The animate-in states run while the intro cinematic is still scrolling and
    // parallaxing backdrop layers out of the resident world region; launching there swaps that region
    // away underneath it, and it never reaches the state that watches for the launch.
    if (menu_state(menu) != layout::kProfileMenuBrowseState)
        return;

    *menu_field(menu, layout::kProfileMenuSlotOff) = static_cast<int>(kScratchSlot);
    if (!stage_save() || !menu_request_state(menu, layout::kProfileMenuLaunchState))
    {
        fail("could not stage the save or drive the profile menu");
        menu_request_state(menu, layout::kProfileMenuBackState);
        return;
    }
    pal::logf(pal::LogLevel::Info, "takeover: profile menu %p driven to launch state on slot %u", menu, kScratchSlot);
    set_step(TakeoverStep::Launching);
}

bool SaveTakeover::stage_save()
{
    // Writes the vanilla save FILE, not the live working slot: the launch copies file over working,
    // so a working-slot write here is discarded.
    if (const auto blob = store_.load(seed_, slot_))
    {
        if (!mod::set_save_slot_contents(kScratchSlot, blob->c_str()))
        {
            pal::logf(pal::LogLevel::Error, "takeover: failed to stage the mod save into slot %u", kScratchSlot);
            return false;
        }
        pal::logf(pal::LogLevel::Info, "takeover: staged existing mod save into slot %u", kScratchSlot);
        return true;
    }
    if (!pal::init_new_save_file(kScratchSlot))
    {
        pal::logf(pal::LogLevel::Error, "takeover: failed to initialize a new save");
        return false;
    }
    return true;
}

void SaveTakeover::on_game_save_requested()
{
    if (step_ != TakeoverStep::Running)
        return;
    flush();
}

void SaveTakeover::flush()
{
    if (mod::save_write_enabled())
    {
        pal::logf(pal::LogLevel::Warn, "takeover: game save writes were re-enabled; suppressing");
        mod::set_save_write_enabled(false);
    }

    // Re-resolve identity rather than trusting seed_/slot_: a reconnect to a different AP session
    // since begin() must not file this blob under the session that is no longer live. Persisting
    // nothing is safe, persisting to the wrong seed is not.
    const auto [current_seed, current_slot] = identity_();
    if (current_seed != seed_ || current_slot != slot_)
    {
        pal::logf(pal::LogLevel::Warn, "takeover: AP identity changed since begin() (was seed=%s slot=%s, now seed=%s slot=%s); skipping flush", seed_.c_str(),
                  slot_.c_str(), current_seed.c_str(), current_slot.c_str());
        return;
    }

    const std::string blob = mod::active_save_slot_contents();
    if (blob.empty())
    {
        pal::logf(pal::LogLevel::Warn, "takeover: empty save blob; skipping flush");
        return;
    }
    if (!store_.store(seed_, slot_, blob))
        pal::logf(pal::LogLevel::Error, "takeover: failed to write mod save for seed=%s slot=%s", seed_.c_str(), slot_.c_str());
}

std::vector<std::string> SaveTakeover::status_lines() const
{
    std::vector<std::string> out;
    out.push_back(std::string("takeover: ") + takeover_step_name(step_));
    if (!seed_.empty())
        out.push_back("takeover save: " + store_.path_for(seed_, slot_).string());
    return out;
}

} // namespace mth
