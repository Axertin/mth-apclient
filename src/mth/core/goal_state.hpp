#pragma once

#include <bit>
#include <cstdint>
#include <vector>

namespace mth
{

// Older seeds carry no slot_data "broken_generators"; every generator counts for them.
inline constexpr std::uint64_t kAllGeneratorsMask = ~std::uint64_t{0};

// slot_data / lamp generator index -> SaveSlot 0x290 generator-fixed bit. These are NOT equal: the game's
// BossComponent::SetGeneratorFixed(bit) decodes the area name from the bit position directly, so the bit is
// {crypt=1, bayou=2, septemburg=3, boneBeach=4, coltranePeak=5, astralOrrery=6}. Indexed by apworld generator
// index (bayou/Noxs=0, crypt/Queensbury=1, septemburg=2, boneBeach=3, coltranePeak=4, astralOrrery=5). Bit 0
// is unused by the game (#146). See docs re-notes 2026-07-17-ossex-fountain-lamps.
inline constexpr int kGeneratorSaveBit[] = {2, 1, 3, 4, 5, 6};
inline constexpr int kGeneratorCount = static_cast<int>(sizeof(kGeneratorSaveBit) / sizeof(kGeneratorSaveBit[0]));

// Fold slot_data generator indices into a mask over the SaveSlot generator-fixed bitfield, translating each
// apworld index to its 0x290 bit. Unknown indices (no such generator) are ignored; the caller warns.
[[nodiscard]] inline std::uint64_t broken_generator_mask(const std::vector<int> &indices) noexcept
{
    std::uint64_t mask = 0;
    for (int i : indices)
        if (i >= 0 && i < kGeneratorCount)
            mask |= (std::uint64_t{1} << static_cast<unsigned>(kGeneratorSaveBit[i]));
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
