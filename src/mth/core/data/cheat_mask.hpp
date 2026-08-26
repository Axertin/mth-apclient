#pragma once

#include <cstdint>

// Word/bit split for the 254-entry modifier ("cheat") bitmask, stored as four 64-bit words covering
// indices 0..63, 64..127, 128..191 and 192..255. Both PAL implementations write this same layout
// into the runtime mask at CheatManager+0x20, so the shift lives here once rather than twice.
//
// cheat_mask_set and cheat_mask_test are total over int: an idx outside 0..255, the full range the
// four-word array can address, is not a memory access at all. cheat_mask_set does nothing and
// cheat_mask_test reads false. A caller enforcing the narrower real cheat count (0..253) still has
// to clear that bound itself; this header only owns the widest range the array's layout can address.
namespace mth
{

// Pure arithmetic, meaningful only for 0 <= idx < 256; out of that range the result is not a valid
// word/bit for the mask, but computing it touches no memory, so nothing needs guarding here. The
// guard lives in cheat_mask_set/cheat_mask_test below, the two functions that actually index into it.
[[nodiscard]] inline constexpr int cheat_mask_word(int idx)
{
    return idx >> 6;
}

[[nodiscard]] inline constexpr std::uint64_t cheat_mask_bit(int idx)
{
    return std::uint64_t{1} << (static_cast<unsigned>(idx) & 63u);
}

inline void cheat_mask_set(std::uint64_t *mask, int idx, bool on)
{
    if (idx < 0 || idx >= 256)
        return;
    if (on)
        mask[cheat_mask_word(idx)] |= cheat_mask_bit(idx);
    else
        mask[cheat_mask_word(idx)] &= ~cheat_mask_bit(idx);
}

[[nodiscard]] inline bool cheat_mask_test(const std::uint64_t *mask, int idx)
{
    if (idx < 0 || idx >= 256)
        return false;
    return (mask[cheat_mask_word(idx)] & cheat_mask_bit(idx)) != 0;
}

} // namespace mth
