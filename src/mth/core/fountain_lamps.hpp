#pragma once

#include <cstdint>
#include <vector>

namespace mth
{

// Ossex fountain: lamp indices 0..5 are the six generators; index 6 is the Radiant Manor prime lamp
// (always lit, never forced).
inline constexpr int kGeneratorLampCount = 6;

// Fold a list of game lamp indices into a bitmask (bit i => force lamp i lit). Values outside
// [0, kGeneratorLampCount) (prime index 6, out-of-range, negative) are ignored.
[[nodiscard]] inline std::uint32_t lit_mask_from_indices(const std::vector<int> &indices)
{
    std::uint32_t mask = 0;
    for (int i : indices)
        if (i >= 0 && i < kGeneratorLampCount)
            mask |= (1u << static_cast<unsigned>(i));
    return mask;
}

} // namespace mth
