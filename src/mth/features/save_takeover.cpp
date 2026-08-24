#include "mth/features/save_takeover.hpp"

#include "mod/mod_api.hpp"
#include "mth/core/data/component_types.hpp"
#include "mth/core/data/game_layout.hpp"
#include "mth/features/scene_walk.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_mem.hpp"
#include "pal/pal_module.hpp"

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
}

SaveTakeover::~SaveTakeover()
{
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

    if (mod_size_ == 0)
    {
        const pal::ModuleInfo gm = pal::game_module();
        mod_base_ = gm.base;
        mod_size_ = gm.size;
    }

    std::size_t visited = 0;
    pending_.clear();
    pending_.push_back(root);
    while (!pending_.empty() && visited < kSceneMaxNodes)
    {
        void *entity = pending_.back();
        pending_.pop_back();

        const std::size_t count = mod::entity_children(entity, nullptr, 0); // sizing call
        if (count == 0)
            continue;
        buffer_.assign(count > kSceneMaxChildren ? kSceneMaxChildren : count, nullptr);
        mod::entity_children(entity, buffer_.data(), buffer_.size());
        for (void *c : buffer_)
        {
            if (!looks_like_component(c, mod_base_, mod_size_))
                continue;
            ++visited;
            // ProfileSelectMenu is a plain ycComponent, so it is a leaf: never descend into it.
            if (mod::component_isa(c, rtti::kProfileSelectMenu))
            {
                drive_profile_menu(c);
                return;
            }
            if (mod::component_isa(c, rtti::kYcEntity))
                pending_.push_back(c);
        }
    }

    // Silence is this walk's failure mode, and here it is also the normal case: the menu only exists for
    // the seconds between the title and the launch. Warn once only when a claim is actually pending and
    // the walk reached nothing at all, which can only mean broken rather than absent.
    if (visited == 0 && step_ == TakeoverStep::AwaitingMenu && !warned_no_walk_)
    {
        warned_no_walk_ = true;
        pal::logf(pal::LogLevel::Warn, "takeover: menu-world scene walk reached no components; the profile menu cannot be found");
    }
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

    // #152: a capture taken outside gameplay is the cleared slot, and storing it destroys the run (which is
    // then staged back as a fresh file, while the AP granted-set still says every item was handed out).
    if (!in_gameplay())
    {
        pal::logf(pal::LogLevel::Warn, "takeover: not in gameplay; skipping flush (the live slot is not the run)");
        return;
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
    out.push_back(std::string("takeover: ") + takeover_step_name(step_));
    if (!seed_.empty())
        out.push_back("takeover save: " + store_.path_for(seed_, slot_).string());
    return out;
}

} // namespace mth
