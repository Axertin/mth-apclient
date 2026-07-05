#pragma once

namespace mth
{

// twin: mth/features/goal_tracker.hpp polls the save against this.
// slot_data "goal_config": which condition completes the AP goal. Unknown/absent -> finish.
inline constexpr int kGoalFinish = 0;     // beat the game (SaveSlot game-clear flag)
inline constexpr int kGoalGenerators = 1; // repair >= goal_generators generators
inline constexpr int kGoalBosses = 2;     // defeat >= goal_bosses bosses

// Whether the configured goal is satisfied by the polled SaveSlot state. Pure so it is unit-testable.
[[nodiscard]] constexpr bool goal_met(int config, int gens_needed, int bosses_needed, bool game_cleared, int gens_done, int bosses_done) noexcept
{
    if (config == kGoalGenerators)
        return gens_done >= gens_needed;
    if (config == kGoalBosses)
        return bosses_done >= bosses_needed;
    return game_cleared;
}

} // namespace mth
