#pragma once

namespace mth
{

// Boss locations occupy a reserved slot range well above the 361 item-collection slots (0..360),
// leaving headroom for future location types. The apworld must mirror: ap loc id = kLocBase + slot.
inline constexpr int kBossLocBase = 1000;

// kBossInfo has 29 entries (positional indices 0x00..0x1C); GetBossIndex returns 0x00..0x1B in
// observed play but scans the whole table, so accept the full range. Garbage reads (>0x1C) are
// rejected as a self-check that the +0x68 offset is still correct.
inline constexpr int kMaxBossIndex = 0x1C;

[[nodiscard]] inline constexpr bool is_boss_index(int boss_index)
{
    return boss_index >= 0 && boss_index <= kMaxBossIndex;
}

// Precondition: is_boss_index(boss_index).
[[nodiscard]] inline constexpr int boss_location_slot(int boss_index)
{
    return kBossLocBase + boss_index;
}

} // namespace mth
