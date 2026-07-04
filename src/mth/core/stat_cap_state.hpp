#pragma once

#include "mth/core/ap/ap_ids.hpp" // kStatCount

namespace mth
{
class ApState;

// slot_data "max_stat_level": per-stat level ceiling, bounded to the game-supported [10, 99]. Absent
// slot_data already arrives as 99 (the game's absolute max). Pure so it is unit-testable.
[[nodiscard]] constexpr int clamp_max_stat_level(int v) noexcept
{
    return v < 10 ? 10 : (v > 99 ? 99 : v);
}

// Ceiling fed to StatCapState::enforced_cap: real stats (0..<kStatCount) take the slot_data max_stat_level,
// replacing the game's native cap; bone bank / out-of-range keep vanilla_cap. Pure so it is unit-testable.
[[nodiscard]] constexpr int stat_cap_ceiling(int stat, int max_stat_level, int vanilla_cap) noexcept
{
    return (stat >= 0 && stat < kStatCount) ? max_stat_level : vanilla_cap;
}

// twin: mth/features/levelcap_hooks.hpp enforces this in-game.
// Per-stat level-cap policy, derived from received AP "cap up" items. Pure logic, no platform deps.
// The game's per-stat buy-gate is `current_level < cap`; we feed it min(vanilla_cap, granted-count),
// so with 0 cap-ups a stat is frozen at its starting level and each cap-up unlocks one more level.
class StatCapState
{
  public:
    // Recompute per-stat granted counts from the AP received-items list. Idempotent; safe every tick.
    void recompute(const ApState &state);

    // Offline test seam: set the granted counts directly (bypasses AP).
    void set_counts(int attack, int defense, int sidearm);

    // The cap to enforce for `stat` given the game's vanilla cap for the active save. Always in the
    // game's own units and never exceeds vanilla. stat out of [0,3) -> vanilla_cap (no restriction).
    [[nodiscard]] int enforced_cap(int stat, int vanilla_cap) const;

    // Raw granted count for a stat (diagnostics). stat out of [0,3) -> 0.
    [[nodiscard]] int granted(int stat) const;

  private:
    int counts_[3]{0, 0, 0};
};

} // namespace mth
