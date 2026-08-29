#include "mth/features/save_takeover.hpp"

#include "mod/mod_api.hpp"
#include "mth/core/data/component_types.hpp"
#include "mth/core/data/game_layout.hpp"
#include "mth/features/scene_walk.hpp"
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

// The scene walk hands back the object's primary pointer on both platforms, so the shared mth::layout
// offsets apply directly - unlike the old detour, where MSVC passed a base subobject that needed a
// per-platform adjust.
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

// The live slot holds the player's run only while gameplay owns it. Returning to the title runs
// SaveSlot::Clear over it and deactivates it, so anything read from there is a blank save.
bool in_gameplay()
{
    const int gs = mod::current_game_state();
    // -1 = accessor unavailable, must not read as "in gameplay". Profile-select is its own gamestate, so
    // excluding only the title screen would read the menu opening as gameplay.
    return gs >= 0 && gs != layout::kTitleGameState && gs != layout::kProfileSelectGameState;
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

SaveTakeover::SaveTakeover(ApSaveBundleStore &store, IdentityFn identity) : store_(store), identity_(std::move(identity))
{
    if (!pal::install_save_request_hook([this] { on_game_save_requested(); }))
        pal::logf(pal::LogLevel::Warn, "takeover: save-request hook unavailable; mod saves will not flush");
    if (!mod::install_input_suppress_hooks())
        pal::logf(pal::LogLevel::Warn, "takeover: input hooks unavailable; a held confirm can still reach the file-select menu");
}

SaveTakeover::~SaveTakeover()
{
    mod::remove_input_suppress_hooks();
    pal::remove_save_request_hook();
}

bool SaveTakeover::begin()
{
    // Without the scene walk nothing would ever find the menu to drive, and the recovery that bounces
    // the player back to the title runs off the same pass. Refuse the launch instead.
    if (!mod::entity_walk_api_available() || !mod::world_menu_root_api_available())
    {
        pal::logf(pal::LogLevel::Error, "takeover: scene-walk API unavailable; refusing to start");
        return false;
    }

    // Running means the player came back to the title from a live session; Failed means the prior attempt
    // gave up. Both re-arm. Every other non-Idle step is mid-sequence. No flush here: begin() only runs from
    // the title, where the slot is already cleared, so it would store a blank save over the run (#152).
    if (step_ != TakeoverStep::Running && !takeover_settled(step_))
        return true; // already claimed, mid-flight

    auto [seed, slot] = identity_();
    seed_ = std::move(seed);
    slot_ = std::move(slot);

    mod::set_save_write_enabled(false); // re-assert; off since connect, this covers a savetest re-enable
    step_ = TakeoverStep::AwaitingMenu;
    frames_in_step_ = 0;
    input_block_.rearm();
    // Here rather than on the next tick: this runs inside the StartGame confirm, and the input update
    // that would carry a still-held button into the file-select menu is the very next one.
    update_input_block();
    // InstallHook succeeds on a name the build never dispatches, so the ctor's return value proves
    // nothing. These fire every input frame, which makes "still silent by the time a launch is claimed"
    // the same statement as "this build has no input hooks", and the footgun is back.
    if (!mod::input_suppress_hooks_fired() && !warned_input_inert_)
    {
        warned_input_inert_ = true;
        pal::logf(pal::LogLevel::Error, "takeover: input hooks have never fired; a held confirm can still open a vanilla file");
    }
    pal::logf(pal::LogLevel::Info, "takeover: begin seed=%s slot=%s", seed_.c_str(), slot_.c_str());
    return true;
}

// The block outlives the steps that want it: a takeover that gave up still has the player sitting on a
// live file-select for the frames the bounce takes.
void SaveTakeover::update_input_block()
{
    // Only the Failed bounce reads this, so the accessor stays off every other tick. An unavailable
    // accessor reads as "menu gone" and releases, the opposite of in_gameplay() above: holding a dead
    // controller on a build that cannot answer is worse than the narrow window it would cover.
    const bool on_profile_select = step_ == TakeoverStep::Failed && mod::current_game_state() == layout::kProfileSelectGameState;

    const bool was_blocked = input_block_.blocked();
    const bool block = input_block_.advance(step_, on_profile_select);
    mod::set_input_suppressed(block); // unconditional, so the flag cannot drift from what this decided
    if (block != was_blocked)
        pal::logf(pal::LogLevel::Info, "takeover: game input %s (step=%s)", block ? "swallowed" : "restored", takeover_step_name(step_));
}

void SaveTakeover::set_step(TakeoverStep next)
{
    pal::logf(pal::LogLevel::Info, "takeover: %s -> %s", takeover_step_name(step_), takeover_step_name(next));
    step_ = next;
    frames_in_step_ = 0;
}

void SaveTakeover::tick()
{
    update_input_block(); // ahead of the early return: Failed still blocks while the menu is up

    if (takeover_settled(step_))
        return; // begin() re-arms these; the flush is driven by the save-request hook, not the tick

    TakeoverInputs in;
    in.save_api_ready = mod::save_api_available();
    in.gameplay_entered = in_gameplay();
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

// The ProfileSelectMenu is docked directly on the menu world's root entity, so this is normally a
// single level; the descent is insurance against a build that nests it. A menu world is tiny compared
// with a room, so this runs every pass rather than on a cadence - the launch should not wait.
void SaveTakeover::on_world_update_end(void *world)
{
    // Only the AwaitingMenu claim and the Failed bounce need the menu; every other step ignores it.
    if (world == nullptr || (step_ != TakeoverStep::AwaitingMenu && step_ != TakeoverStep::Failed))
        return;
    if (!mod::entity_walk_api_available() || !mod::world_menu_root_api_available())
        return;
    void *root = mod::world_menu_root_entity(world);
    if (root == nullptr)
        return;

    // ProfileSelectMenu is a plain ycComponent, so it is a leaf: the walk never descends into it.
    const SceneWalk walk = walker_.find_first(root, rtti::kProfileSelectMenu, [&](void *c) { drive_profile_menu(c); });
    // Silence is also the normal case here: the menu only exists for the seconds between the title and
    // the launch, so this is only a real signal while a claim is waiting on it.
    walker_.report_silence(walk, step_ == TakeoverStep::AwaitingMenu);
}

void SaveTakeover::drive_profile_menu(void *menu)
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
    // Ahead of the step test on purpose: without it, "the game never asked to save" and "we declined
    // to commit" leave the same empty log.
    pal::logf(pal::LogLevel::Debug, "takeover: game save requested (step=%s)", takeover_step_name(step_));
    flush();
}

void SaveTakeover::flush()
{
    const auto [current_seed, current_slot] = identity_();
    if (const char *refusal = flush_refusal({step_, in_gameplay(), seed_, slot_, current_seed, current_slot}); refusal != nullptr)
    {
        // The game asking while we hold no claim is every vanilla save, so it stays at Debug. The rest
        // mean a commit the player expected did not happen.
        const pal::LogLevel level = step_ != TakeoverStep::Running ? pal::LogLevel::Debug : pal::LogLevel::Warn;
        pal::logf(level, "takeover: skipping flush (%s; step=%s seed=%s slot=%s now seed=%s slot=%s)", refusal, takeover_step_name(step_), seed_.c_str(),
                  slot_.c_str(), current_seed.c_str(), current_slot.c_str());
        return;
    }

    // Only under a live claim: outside one the game's own writes are none of our business, and
    // suppressing them here would silently break vanilla saving for a player with no AP session.
    if (mod::save_write_enabled())
    {
        pal::logf(pal::LogLevel::Warn, "takeover: game save writes were re-enabled; suppressing");
        mod::set_save_write_enabled(false);
    }

    const std::string blob = mod::active_save_slot_contents();
    if (blob.empty())
    {
        pal::logf(pal::LogLevel::Warn, "takeover: empty save blob; skipping flush");
        return;
    }
    // The only place AP state reaches the disk, so a log here is the evidence that a run's progress
    // and its checks were committed together.
    if (!store_.store(seed_, slot_, blob))
        pal::logf(pal::LogLevel::Error, "takeover: failed to write mod save for seed=%s slot=%s", seed_.c_str(), slot_.c_str());
    else
        pal::logf(pal::LogLevel::Debug, "takeover: committed save and ap state for seed=%s slot=%s", seed_.c_str(), slot_.c_str());
}

std::vector<std::string> SaveTakeover::status_lines() const
{
    std::vector<std::string> out;
    out.push_back(std::string("takeover: ") + takeover_step_name(step_) + (input_block_.blocked() ? " (game input swallowed)" : ""));
    if (!seed_.empty())
        out.push_back("takeover save: " + store_.path_for(seed_, slot_).string());
    return out;
}

} // namespace mth
