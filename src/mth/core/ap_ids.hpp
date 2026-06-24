#pragma once

#include <cstdint>

namespace mth
{

//   ITEM space                          LOCATION space
//     0  [0..999]     vanilla itemType    0  [0..999]     collection slots (kLocBase + slot)
//     1  [1000..1999] progressive         1  [1000..1999] boss defeats (kBossLocBase + index)
//     2  [2000..2999] kear blocks         2+ reserved
//     3  [3000..3999] abilities
//     4  [4000..4999] blockers
//     5  [5000..5999] traps
//     6+ reserved

// Vanilla game items
// ap item id = kItemBase + game itemType (0..194; 0 = engine "None", reserved).
inline constexpr std::int64_t kItemBase = 0;

inline constexpr std::int64_t ap_item_id(int item_type)
{
    return kItemBase + item_type;
}

inline constexpr int game_item_type(std::int64_t ap_item_id_)
{
    return static_cast<int>(ap_item_id_ - kItemBase);
}

// Capacity upgrades: vanilla itemTypes 68..72 (Magic, Health, Spark, Vial, Trinket), stored as a
// popcount of owned bits per type, so OnPickupDone can't stack them. UpgradeState counts receipts and
// pal::apply_upgrades sets that many bits; the granter skips them. Cap = vanilla location count.
inline constexpr int kUpgradeItemBase = 68;
inline constexpr int kUpgradeCount = 5;
inline constexpr int kUpgradeCaps[kUpgradeCount] = {10, 18, 4, 10, 6}; // Magic, Health, Spark, Vial, Trinket (full-game counts)

inline constexpr bool is_capacity_upgrade_item(std::int64_t ap_item_id_)
{
    return ap_item_id_ >= kUpgradeItemBase && ap_item_id_ < kUpgradeItemBase + kUpgradeCount;
}

inline constexpr int upgrade_index(std::int64_t ap_item_id_)
{
    return static_cast<int>(ap_item_id_ - kUpgradeItemBase); // valid only when is_capacity_upgrade_item()
}
// Progressive Items
// Count-based: the Nth receipt of a chain's id is "tier N". Sub-layout:
//   1000..1004  weapon families  (kProgWeaponBase + family)
//   1005..1007  per-stat cap-ups (kProgStatCapBase + stat)
//   1008        all-stat cap-up  (kProgStatCapAllId)

inline constexpr std::int64_t kProgressiveItemBase = 1000;
inline constexpr int kStatCount = 3; // 0=attack, 1=defense, 2=sidearm/magic

inline constexpr bool is_progressive_item(std::int64_t ap_item_id_)
{
    return ap_item_id_ >= kProgressiveItemBase && ap_item_id_ < kProgressiveItemBase + 1000;
}

// One chain per weapon family
inline constexpr std::int64_t kProgWeaponBase = kProgressiveItemBase;
inline constexpr int kWeaponFamilyCount = 5;
inline constexpr int kWeaponTiers = 3;
inline constexpr int kWeaponItemTypes[kWeaponFamilyCount][kWeaponTiers] = {
    {2, 3, 4},    // Whip
    {5, 6, 7},    // Hammer
    {8, 9, 10},   // Daggers
    {11, 12, 13}, // Buster Bat
    {14, 15, 16}, // Casket
};

inline constexpr bool is_weapon_item(std::int64_t ap_item_id_)
{
    return ap_item_id_ >= kProgWeaponBase && ap_item_id_ < kProgWeaponBase + kWeaponFamilyCount;
}

inline constexpr int weapon_family(std::int64_t ap_item_id_)
{
    return static_cast<int>(ap_item_id_ - kProgWeaponBase); // valid only when is_weapon_item()
}

// Engine itemType for family, -1 if out of range.
inline constexpr int weapon_itemtype(int family, int tier)
{
    if (family < 0 || family >= kWeaponFamilyCount || tier < 1 || tier > kWeaponTiers)
        return -1;
    return kWeaponItemTypes[family][tier - 1];
}

// Stat-cap cap-ups
// derived state (StatCapState), never granted as items. The all-stat id raises
// every stat per receipt.
inline constexpr std::int64_t kProgStatCapBase = kProgWeaponBase + kWeaponFamilyCount; // per-stat: +stat
inline constexpr std::int64_t kProgStatCapAllId = kProgStatCapBase + kStatCount;

inline constexpr bool is_stat_cap_all_item(std::int64_t ap_item_id_)
{
    return ap_item_id_ == kProgStatCapAllId;
}

inline constexpr bool is_stat_cap_item(std::int64_t ap_item_id_)
{
    return (ap_item_id_ >= kProgStatCapBase && ap_item_id_ < kProgStatCapBase + kStatCount) || is_stat_cap_all_item(ap_item_id_);
}

inline constexpr int stat_cap_item_stat(std::int64_t ap_item_id_)
{
    return static_cast<int>(ap_item_id_ - kProgStatCapBase); // valid only for a per-stat id
}

// item-category bases
inline constexpr std::int64_t kKearBlockItemBase = 2000; // kear-lock removals (wired)
inline constexpr std::int64_t kAbilityItemBase = 3000;   // ability gates (Ability enum; see ability_ids.hpp)
inline constexpr std::int64_t kBlockerItemBase = 4000;   // reserved
inline constexpr std::int64_t kTrapItemBase = 5000;      // reserved

inline constexpr bool is_vanilla_game_item(std::int64_t ap_item_id_)
{
    return ap_item_id_ >= kItemBase && ap_item_id_ < kProgressiveItemBase;
}

// Kear blocks
// id = kKearBlockItemBase + engine id
inline constexpr bool is_kear_block_item(std::int64_t ap_item_id_)
{
    return ap_item_id_ >= kKearBlockItemBase && ap_item_id_ < kAbilityItemBase;
}

inline constexpr int kear_block_engine_id(std::int64_t ap_item_id_)
{
    return static_cast<int>(ap_item_id_ - kKearBlockItemBase); // valid only when is_kear_block_item()
}

// locations

// Item-collection pickup/shop slots: ap loc id = kLocBase + slot (location segment 0).
inline constexpr std::int64_t kLocBase = 0;

inline constexpr std::int64_t ap_loc_id(int collection_idx)
{
    return kLocBase + collection_idx;
}

// Boss-defeat locations occupy location segment 1.
inline constexpr int kBossLocBase = 1000;

// kBossInfo has 29 entries (positional indices 0x00..0x1C); GetBossIndex returns 0x00..0x1B in
// observed play but scans the whole table, so accept the full range.
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

// GigaLionel
inline constexpr int kGoalBossIndex = 8;

} // namespace mth
