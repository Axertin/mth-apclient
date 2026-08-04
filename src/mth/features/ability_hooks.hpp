#pragma once

#include <cstdint>
#include <functional>

#include "mth/core/ability_gate.hpp"
#include "mth/core/burrow_boundary.hpp"
#include "mth/core/data/ability_ids.hpp"

namespace mth
{

// twin: mth/core/ability_gate.hpp (pure gate decision).
// Drives the PAL ability-gate seam from AP state. The block lambda installed in the ctor runs on the
// game thread (invoked by the detours); enforce_/gate_ are written only from the game-thread per-tick
// path, so plain members suffice (one-frame staleness acceptable).
class AbilityHooks
{
  public:
    explicit AbilityHooks(std::function<bool(std::int64_t item_id)> is_granted);
    ~AbilityHooks();
    AbilityHooks(const AbilityHooks &) = delete;
    AbilityHooks &operator=(const AbilityHooks &) = delete;

    void set_randomized(Ability a, bool on);
    void set_enforce(bool on); // authed && active save slot is the AP slot
    // Per-destination train gating (train_rando). When active, enforce_train_tick clamps the SaveSlot
    // unlocked-lines bitfield to line_mask (AP-granted tickets) instead of running the whole-train ability
    // gate, so a station visit no longer auto-unlocks its destination (#98).
    void set_train_gate(bool rando_active, std::uint32_t line_mask);
    void enforce_train_tick(); // game-thread; clamps train destinations (rando) or the train-present byte
    // Burrow and Swim are separate items, but the game flips between them mid-burrow through a path the
    // SetBurrowGround gate never sees (#163). Poll the boundary each tick and undo a crossing into the mode
    // the player does not own: drown in deep water without Swim, surface on land without Burrow.
    void enforce_burrow_tick(void *player);
    void on_world_destroy(); // drop the burrow arm: a room load ends a burrow without an emerge commit

  private:
    std::function<bool(std::int64_t)> is_granted_;
    AbilityGate gate_;
    BurrowBoundaryGate burrow_;
    void *last_player_{nullptr}; // burrow arm is per-player; a changed/null pointer disarms (stale-arm safety)
    bool enforce_{false};
    bool train_rando_active_{false};
    std::uint32_t train_mask_{0};
    std::uintptr_t g_save_manager_{0};
};

} // namespace mth
