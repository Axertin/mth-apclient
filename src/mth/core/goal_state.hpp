#pragma once

#include <bit>
#include <cstdint>
#include <vector>

namespace mth
{

// Older seeds carry no slot_data "broken_generators"; every generator counts for them.
inline constexpr std::uint64_t kAllGeneratorsMask = ~std::uint64_t{0};

// Fold slot_data generator indices into a mask over the SaveSlot generator-fixed bitfield. Indices outside
// its 64 bits are ignored; the caller warns.
[[nodiscard]] inline std::uint64_t broken_generator_mask(const std::vector<int> &indices) noexcept
{
    std::uint64_t mask = 0;
    for (int i : indices)
        if (i >= 0 && i < 64)
            mask |= (std::uint64_t{1} << static_cast<unsigned>(i));
    return mask;
}

// Only generators the seed started broken count (#141): the rest are already flagged fixed in the same
// bitfield, so an unmasked popcount meets the threshold on a fresh save.
[[nodiscard]] inline int generators_done(std::uint64_t generator_bits, std::uint64_t broken_mask) noexcept
{
    return std::popcount(generator_bits & broken_mask);
}

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
