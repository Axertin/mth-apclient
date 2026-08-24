#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "mth/core/save/ap_save_bundle.hpp"
#include "mth/core/save/takeover_state.hpp"

namespace mth
{

// Runs the game on a mod-owned save without the player picking a file. The launch is the game's own:
// the mod stages its save into a vanilla slot and drives the profile-select menu into its launch
// state (see mth::layout::kProfileMenuLaunchState). Vanilla save writes are off from the moment AP
// connects, so saveData.yc and the Steam Cloud copy behind it are never touched; persistence is our
// own, flushed on the game's save cadence.
class SaveTakeover
{
  public:
    using IdentityFn = std::function<std::pair<std::string, std::string>()>; // (seed, slot)

    // The store outlives this: App owns it and declares it ahead of the hook manager.
    SaveTakeover(ApSaveBundleStore &store, IdentityFn identity);
    ~SaveTakeover();

    SaveTakeover(const SaveTakeover &) = delete;
    SaveTakeover &operator=(const SaveTakeover &) = delete;

    // Claims the launch. The vanilla StartGame must then run: the substate it requests is what builds
    // the profile-select menu this drives. False means the launch must be blocked, not passed through.
    bool begin();

    void tick();
    void on_game_save_requested();

    // Finds the live profile-select menu in the menu world's scene graph and drives it. Called once per
    // World::Update end; world may be null. The menu is never cached: the intro cinematic owns its
    // lifetime, so it is re-found each pass.
    void on_world_update_end(void *world);

    // True from claim until settled. The dev console's save-write toggle must refuse while this
    // holds, or it leaks the mod-owned session into the player's vanilla saveData.yc.
    [[nodiscard]] bool takeover_active() const
    {
        return !takeover_settled(step_) || step_ == TakeoverStep::Running;
    }

    [[nodiscard]] std::vector<std::string> status_lines() const;

  private:
    void update_input_block();
    void drive_profile_menu(void *menu);
    void flush();
    bool stage_save();
    void fail(const char *why);
    void set_step(TakeoverStep next);

    ApSaveBundleStore &store_;
    IdentityFn identity_;
    TakeoverStep step_{TakeoverStep::Idle};
    int frames_in_step_{0};
    std::uintptr_t mod_base_{0};
    std::size_t mod_size_{0};
    bool warned_no_walk_{false};
    bool warned_input_inert_{false};
    InputBlockState input_block_;
    std::vector<void *> pending_;
    std::vector<void *> buffer_;
    std::string seed_;
    std::string slot_;
};

} // namespace mth
