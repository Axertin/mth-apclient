#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "mth/core/save/ap_save_store.hpp"
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

    SaveTakeover(ApSaveStore store, IdentityFn identity);
    ~SaveTakeover();

    SaveTakeover(const SaveTakeover &) = delete;
    SaveTakeover &operator=(const SaveTakeover &) = delete;

    // Claims the launch. The vanilla StartGame must then run: the substate it requests is what builds
    // the profile-select menu this drives. False means the launch must be blocked, not passed through.
    bool begin();

    void tick();
    void on_game_save_requested();

    // The live profile-select menu, once per update while it exists. Never cached: the intro
    // cinematic owns the menu's lifetime.
    void on_profile_menu(void *menu);

    // True from claim until settled. The dev console's save-write toggle must refuse while this
    // holds, or it leaks the mod-owned session into the player's vanilla saveData.yc.
    [[nodiscard]] bool takeover_active() const
    {
        return !takeover_settled(step_) || step_ == TakeoverStep::Running;
    }

    [[nodiscard]] std::vector<std::string> status_lines() const;

  private:
    void flush();
    bool stage_save();
    void fail(const char *why);
    void set_step(TakeoverStep next);

    ApSaveStore store_;
    IdentityFn identity_;
    TakeoverStep step_{TakeoverStep::Idle};
    int frames_in_step_{0};
    bool menu_hook_ok_{false};
    std::string seed_;
    std::string slot_;
};

} // namespace mth
