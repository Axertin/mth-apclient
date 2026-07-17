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
inline constexpr int kVialUpgradeIndex = 3;                            // Vial's slot in the index order above

// SaveSlot bitfield value granting `count` of capacity-upgrade `upgrade_index` (low `count` bits;
// popcount == capacity). Vials overwrite `current` instead of OR-ing it: the vanilla new file seeds a
// 3-bit vial base an OR can't clear (#83), and the field is vial-only so overwriting is safe.
[[nodiscard]] inline constexpr std::uint32_t upgrade_field_value(int upgrade_index, int count, std::uint32_t current) noexcept
{
    const std::uint32_t bits = count <= 0 ? 0u : (count >= 32 ? 0xFFFFFFFFu : (1u << count) - 1u);
    return upgrade_index == kVialUpgradeIndex ? bits : (current | bits);
}

// Plausibility bound for a capacity-upgrade SaveSlot field read *before* we write it: a valid field is a
// low-bit popcount mask holding at most kUpgradeCaps[index] bits, so it never exceeds this max. A read above
// it means the offset drifted onto unrelated memory (a pointer/float/large counter) - the caller fails closed.
// Depends on a well-formed apworld never granting more than kUpgradeCaps[index] of an upgrade: an over-grant
// writes a field above this bound, so the NEXT resend reads it back as out-of-domain and disables upgrades
// for the session.
[[nodiscard]] inline constexpr bool upgrade_field_in_domain(int upgrade_index, std::uint32_t value) noexcept
{
    const int cap = (upgrade_index >= 0 && upgrade_index < kUpgradeCount) ? kUpgradeCaps[upgrade_index] : 0;
    const std::uint32_t max = cap >= 32 ? 0xFFFFFFFFu : (1u << cap) - 1u;
    return value <= max;
}

// New held-vial count that preserves the missing amount (old_max - old_held) across a capacity change,
// clamped to [0, new_max]. So a capacity grant mid-run raises the ceiling without refilling flasks, and a
// full player stays full. Used by the mod-API vial path (PlayerSetMaxVials/PlayerSetVials).
[[nodiscard]] inline constexpr int maintained_vial_held(int old_max, int old_held, int new_max) noexcept
{
    const int held = old_held + (new_max - old_max);
    return held < 0 ? 0 : (held > new_max ? new_max : held);
}

inline constexpr bool is_capacity_upgrade_item(std::int64_t ap_item_id_)
{
    return ap_item_id_ >= kUpgradeItemBase && ap_item_id_ < kUpgradeItemBase + kUpgradeCount;
}

inline constexpr int upgrade_index(std::int64_t ap_item_id_)
{
    return static_cast<int>(ap_item_id_ - kUpgradeItemBase); // valid only when is_capacity_upgrade_item()
}

// Train fast-travel: the 5 destinations are vanilla itemTypes 0x5f..0x63 (95..99, "tickets"). Whether a
// destination is travelable lives in a SaveSlot unlocked-lines bitfield, one bit per line
// (bit = 1 << (itemType - kTrainTicketItemTypeBase)). AP unlocks a destination by setting its bit; the game
// otherwise sets it for free just by visiting the station (#98), so the client clamps the field to the mask
// of AP-granted tickets each frame.
inline constexpr int kTrainTicketItemTypeBase = 0x5f; // itemType 95 == train line 0
inline constexpr int kTrainLineCount = 5;

[[nodiscard]] inline constexpr bool is_train_ticket_item_type(int item_type) noexcept
{
    return item_type >= kTrainTicketItemTypeBase && item_type < kTrainTicketItemTypeBase + kTrainLineCount;
}

// The unlocked-lines bit for a train-ticket itemType, or 0 when item_type is not a ticket.
[[nodiscard]] inline constexpr std::uint32_t train_ticket_bit(int item_type) noexcept
{
    return is_train_ticket_item_type(item_type) ? (1u << (item_type - kTrainTicketItemTypeBase)) : 0u;
}

// Cancel a picked line whose AP ticket isn't granted (96-99). Line 0 (Ossex/HUB, 95) rides on the Train
// Pass alone, so it is never blocked; non-ticket codes (100 Exit, 101 locked box) aren't either.
[[nodiscard]] inline constexpr bool train_destination_blocked(int selected_code, std::uint32_t granted_mask) noexcept
{
    if (selected_code == kTrainTicketItemTypeBase) // 95 = Ossex/HUB, always rideable with the pass
        return false;
    const std::uint32_t bit = train_ticket_bit(selected_code);
    return bit != 0 && (granted_mask & bit) == 0;
}

// Progressive Items
// Count-based: the Nth receipt of a chain's id is "tier N". Sub-layout:
//   1000..1004  weapon families  (kProgWeaponBase + family)
//   1005..1007  per-stat cap-ups (kProgStatCapBase + stat)
//   1008        all-stat cap-up  (kProgStatCapAllId)
//   1009        fishing rod      (kProgFishingRodId)

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

inline constexpr std::int64_t kMapItem = kProgressiveItemBase + 10;
inline constexpr int kMapTiers = 2;
// world_map (75), enhanced_map (76) granted in tier order.
inline constexpr int kMapItemTypes[kMapTiers] = {75, 76};
inline constexpr bool is_map_item(std::int64_t ap_item_id_)
{
    return ap_item_id_ >= kProgWeaponBase && ap_item_id_ == kMapItem;
}

inline constexpr int map_itemtype(int tier)
{
    if (tier < 1 || tier > kMapTiers)
        return -1;
    return kMapItemTypes[tier - 1];
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

// Fishing rod: single progressive chain; the Nth receipt grants the Nth fishing-rod upgrade itemType.
inline constexpr std::int64_t kProgFishingRodId = kProgStatCapAllId + 1; // 1009
inline constexpr int kFishingRodTiers = 3;
// Upgrade_FishingRod (87), Upgrade_FishingUpgrade (88), Upgrade_FishingGold (89), granted in tier order.
inline constexpr int kFishingRodItemTypes[kFishingRodTiers] = {87, 88, 89};

inline constexpr bool is_fishing_rod_item(std::int64_t ap_item_id_)
{
    return ap_item_id_ == kProgFishingRodId;
}

// Wallet (bone-wallet) capacity: count-based derived state (see WalletCapState), never granted as an
// item. Each receipt raises the enforced bone cap; the game itself is not told, the mod clamps bones.
inline constexpr std::int64_t kProgWalletId = kProgressiveItemBase + 11; // 1011

inline constexpr bool is_wallet_item(std::int64_t ap_item_id_)
{
    return ap_item_id_ == kProgWalletId;
}

// Engine itemType for a 1-based tier, -1 if out of range.
inline constexpr int fishing_rod_itemtype(int tier)
{
    if (tier < 1 || tier > kFishingRodTiers)
        return -1;
    return kFishingRodItemTypes[tier - 1];
}

// item-category bases
inline constexpr std::int64_t kKearBlockItemBase = 2000; // kear-lock removals (wired)
inline constexpr std::int64_t kAbilityItemBase = 3000;   // ability gates (Ability enum; see ability_ids.hpp)
inline constexpr std::int64_t kBlockerItemBase = 4000;   // reserved
inline constexpr std::int64_t kTrapItemBase = 5000;      // reserved

// special Constants
inline constexpr std::int64_t kMMFirstDoubleKearBlockID = 2304;
inline constexpr std::int64_t kMMSecondDoubleKearBlockID = 2306;

inline constexpr bool is_vanilla_game_item(std::int64_t ap_item_id_)
{
    return ap_item_id_ >= kItemBase && ap_item_id_ < kProgressiveItemBase;
}

// Universal Kear (vanilla kear mode): the pool carries this item and each receipt must grant one usable
// key. Its AP id is the kear itemType 0x3f (63) in the vanilla item segment.
inline constexpr std::int64_t kUniversalKearItemType = 0x3f;

inline constexpr bool is_vanilla_kear_item(std::int64_t ap_item_id_)
{
    return ap_item_id_ == ap_item_id(kUniversalKearItemType);
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

// Legovich (NPCBehavior_WeaponMerchant) weapon-upgrade shop slots. Their AP-checked count drives the shop's
// out-of-stock total, which gates the Armand encounter; that count must reflect purchases, not weapons received
// from the multiworld (#67 follow-up), but only when the shop itself is asking.
inline constexpr int kLegovichLocations[] = {174, 175, 176, 177, 178};

[[nodiscard]] inline constexpr bool is_legovich_location(int loc)
{
    for (int l : kLegovichLocations)
        if (l == loc)
            return true;
    return false;
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

} // namespace mth
