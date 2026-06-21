#pragma once

#include <cstdint>

namespace mth
{

// The complete AP id contract with the apworld generator. Any change here must be
// mirrored in the apworld's location/item tables.
//
// Locations: ap loc id = kLocBase + slot.
//   slots 0..360     : s_rItemCollection pickup/shop slots
//   slots 1000..1028 : boss defeats (kBossLocBase + boss index)
// Items: ap item id = kItemBase + game itemType (0..194; 0 = engine "None", reserved).
//   ids 1000..1002   : virtual per-stat cap-up items (kStatCapItemBase + stat)
inline constexpr std::int64_t kLocBase = 0;
inline constexpr std::int64_t kItemBase = 0;

inline constexpr std::int64_t ap_loc_id(int collection_idx)
{
    return kLocBase + collection_idx;
}

inline constexpr std::int64_t ap_item_id(int item_type)
{
    return kItemBase + item_type;
}

inline constexpr int game_item_type(std::int64_t ap_item_id_)
{
    return static_cast<int>(ap_item_id_ - kItemBase);
}

// Stat-cap "cap up" items are NOT real game items. Reserve a virtual id range above the game-item
// id space (kItemBase..kItemBase+194): one id per stat. Receiving one raises that stat's level cap
// by 1 (see StatCapState). The InboundGranter skips these; they are derived state, not one-shot grants.
inline constexpr std::int64_t kStatCapItemBase = 1000;
inline constexpr int kStatCount = 3; // 0=attack, 1=defense, 2=sidearm/magic

inline constexpr bool is_stat_cap_item(std::int64_t ap_item_id_)
{
    return ap_item_id_ >= kStatCapItemBase && ap_item_id_ < kStatCapItemBase + kStatCount;
}

inline constexpr int stat_cap_item_stat(std::int64_t ap_item_id_)
{
    return static_cast<int>(ap_item_id_ - kStatCapItemBase); // valid only when is_stat_cap_item()
}

// Boss locations occupy a reserved slot range well above the 361 item-collection slots (0..360),
// leaving headroom for future location types. (NB: this overlaps numerically with
// kStatCapItemBase — fine, locations and items are separate id namespaces in AP.)
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

// GigaLionel = the final boss; defeating it = beating the game -> send the AP goal. The index value 8
// is build-stable (positional kBossInfo slot, keyed by name hash); only the +0x68 read offset drifts.
inline constexpr int kGoalBossIndex = 8;

} // namespace mth
