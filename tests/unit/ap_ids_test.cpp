#include <catch2/catch_test_macros.hpp>

#include "mth/core/ap/ap_ids.hpp"

TEST_CASE("ap_item_id and game_item_type round-trip", "[ap_ids]")
{
    REQUIRE(mth::ap_item_id(0) == mth::kItemBase);
    REQUIRE(mth::ap_item_id(42) == mth::kItemBase + 42);
    REQUIRE(mth::game_item_type(mth::ap_item_id(42)) == 42);
    REQUIRE(mth::game_item_type(mth::kItemBase) == 0);
}

TEST_CASE("vanilla kear item id is the Universal Kear itemType", "[ap_ids]")
{
    REQUIRE(mth::is_vanilla_kear_item(mth::ap_item_id(63))); // Universal Kear = game itemType 0x3f
    REQUIRE(mth::is_vanilla_kear_item(63));
    REQUIRE_FALSE(mth::is_vanilla_kear_item(mth::ap_item_id(62)));
    REQUIRE_FALSE(mth::is_vanilla_kear_item(mth::kKearBlockItemBase)); // a kear-lock AP item, not the Universal Kear
}

TEST_CASE("stat-cap item ids: per-stat and all-stat flavours", "[ap_ids]")
{
    REQUIRE(mth::is_stat_cap_item(mth::kProgStatCapBase) == true);
    REQUIRE(mth::is_stat_cap_item(mth::kProgStatCapBase + 2) == true);
    REQUIRE(mth::is_stat_cap_item(mth::ap_item_id(42)) == false); // a real game item
    REQUIRE(mth::stat_cap_item_stat(mth::kProgStatCapBase) == 0);
    REQUIRE(mth::stat_cap_item_stat(mth::kProgStatCapBase + 2) == 2);

    REQUIRE(mth::is_stat_cap_all_item(mth::kProgStatCapAllId));
    REQUIRE(mth::is_stat_cap_item(mth::kProgStatCapAllId)); // umbrella covers the all-stat id
    REQUIRE_FALSE(mth::is_stat_cap_all_item(mth::kProgStatCapBase));
}

TEST_CASE("weapon ids map family+tier to engine itemTypes", "[ap_ids]")
{
    REQUIRE(mth::is_weapon_item(mth::kProgWeaponBase));        // Whip family
    REQUIRE(mth::is_weapon_item(mth::kProgWeaponBase + 4));    // Casket family
    REQUIRE_FALSE(mth::is_weapon_item(mth::kProgStatCapBase)); // stat-cap, not a weapon
    REQUIRE(mth::weapon_family(mth::kProgWeaponBase + 3) == 3);

    REQUIRE(mth::weapon_itemtype(0, 1) == 2);  // Whip
    REQUIRE(mth::weapon_itemtype(0, 3) == 4);  // WhipLevel3
    REQUIRE(mth::weapon_itemtype(4, 1) == 14); // Casket
    REQUIRE(mth::weapon_itemtype(4, 3) == 16); // CasketLevel3
    REQUIRE(mth::weapon_itemtype(0, 4) == -1); // beyond top tier
    REQUIRE(mth::weapon_itemtype(5, 1) == -1); // no such family
}

TEST_CASE("item-space segments are 1000-wide and ordered", "[ap_ids]")
{
    REQUIRE(mth::kItemBase == 0);
    REQUIRE(mth::kProgressiveItemBase == 1000);
    REQUIRE(mth::kKearBlockItemBase == 2000);
    REQUIRE(mth::kAbilityItemBase == 3000);
    REQUIRE(mth::kBlockerItemBase == 4000);
    REQUIRE(mth::kTrapItemBase == 5000);
    // Progressive sub-layout stays within segment 1.
    REQUIRE(mth::is_progressive_item(mth::kProgWeaponBase));
    REQUIRE(mth::is_progressive_item(mth::kProgStatCapAllId));
    REQUIRE(mth::kProgStatCapAllId < mth::kKearBlockItemBase);
}

TEST_CASE("is_vanilla_game_item recognises only item segment 0", "[ap_ids]")
{
    REQUIRE(mth::is_vanilla_game_item(mth::ap_item_id(0)));
    REQUIRE(mth::is_vanilla_game_item(mth::ap_item_id(194)));
    REQUIRE_FALSE(mth::is_vanilla_game_item(mth::kProgWeaponBase));    // weapon (seg 1)
    REQUIRE_FALSE(mth::is_vanilla_game_item(mth::kProgStatCapBase));   // stat-cap (seg 1)
    REQUIRE_FALSE(mth::is_vanilla_game_item(mth::kKearBlockItemBase)); // kear (seg 2)
    REQUIRE_FALSE(mth::is_vanilla_game_item(mth::kAbilityItemBase));   // reserved (seg 3)
    REQUIRE_FALSE(mth::is_vanilla_game_item(mth::kBlockerItemBase));   // reserved (seg 4)
    REQUIRE_FALSE(mth::is_vanilla_game_item(mth::kTrapItemBase));      // reserved (seg 5)
    REQUIRE_FALSE(mth::is_vanilla_game_item(-1));
}

TEST_CASE("boss_location_slot maps index into the reserved range", "[boss]")
{
    REQUIRE(mth::boss_location_slot(0) == mth::kBossLocBase);
    REQUIRE(mth::boss_location_slot(5) == mth::kBossLocBase + 5);
    REQUIRE(mth::kBossLocBase >= 361);
}

TEST_CASE("is_boss_index rejects out-of-range indices", "[boss]")
{
    REQUIRE(mth::is_boss_index(0));
    REQUIRE(mth::is_boss_index(mth::kMaxBossIndex));
    REQUIRE_FALSE(mth::is_boss_index(-1));
    REQUIRE_FALSE(mth::is_boss_index(mth::kMaxBossIndex + 1));
    REQUIRE_FALSE(mth::is_boss_index(64));
}

TEST_CASE("is_legovich_location matches only the WeaponMerchant shop slots", "[legovich]")
{
    REQUIRE(mth::is_legovich_location(174));
    REQUIRE(mth::is_legovich_location(178));
    REQUIRE_FALSE(mth::is_legovich_location(173));
    REQUIRE_FALSE(mth::is_legovich_location(179));
    REQUIRE_FALSE(mth::is_legovich_location(-1));
}

TEST_CASE("upgrade_field_value: vials are authoritative and clear the vanilla base-3 (#83)", "[upgrade]")
{
    // Vanilla seeds 3 vials as SaveSlot+0x18c = 0x7; OR could never reduce it, so the count floored at 3.
    REQUIRE(mth::upgrade_field_value(mth::kVialUpgradeIndex, 0, 0x7u) == 0x0u);
    REQUIRE(mth::upgrade_field_value(mth::kVialUpgradeIndex, 1, 0x7u) == 0x1u);
    REQUIRE(mth::upgrade_field_value(mth::kVialUpgradeIndex, 2, 0x7u) == 0x3u);
    REQUIRE(mth::upgrade_field_value(mth::kVialUpgradeIndex, 3, 0x0u) == 0x7u);
    REQUIRE(mth::upgrade_field_value(mth::kVialUpgradeIndex, 10, 0xFFFFu) == 0x3FFu); // overrides stale bits
}

TEST_CASE("upgrade_field_value: non-vial pools accumulate onto the current bits", "[upgrade]")
{
    REQUIRE(mth::upgrade_field_value(1, 0, 0x5u) == 0x5u); // count 0 leaves the field untouched
    REQUIRE(mth::upgrade_field_value(1, 2, 0x4u) == 0x7u); // OR: 0x4 | 0x3
    REQUIRE(mth::upgrade_field_value(0, 3, 0x0u) == 0x7u);
}

TEST_CASE("train tickets map itemTypes 95-99 to destination-line bits", "[train]")
{
    REQUIRE(mth::is_train_ticket_item_type(0x5f));       // line 0 (95)
    REQUIRE(mth::is_train_ticket_item_type(0x63));       // line 4 (99)
    REQUIRE_FALSE(mth::is_train_ticket_item_type(0x5e)); // 94, just below
    REQUIRE_FALSE(mth::is_train_ticket_item_type(0x64)); // 100, just above
    REQUIRE_FALSE(mth::is_train_ticket_item_type(0));

    REQUIRE(mth::train_ticket_bit(0x5f) == 0x1u);  // 1 << 0
    REQUIRE(mth::train_ticket_bit(0x60) == 0x2u);  // 1 << 1
    REQUIRE(mth::train_ticket_bit(0x63) == 0x10u); // 1 << 4
    REQUIRE(mth::train_ticket_bit(0x64) == 0x0u);  // not a ticket
    REQUIRE(mth::train_ticket_bit(41) == 0x0u);    // a normal item
}

TEST_CASE("train_destination_blocked cancels un-granted ticket lines", "[train]")
{
    // Line 0 (Ossex/HUB, 95) is the free hub: rideable on the Train Pass alone, so it is NEVER blocked.
    REQUIRE_FALSE(mth::train_destination_blocked(0x5f, 0x00)); // line 0 (Ossex) -> always allowed
    REQUIRE_FALSE(mth::train_destination_blocked(0x5f, 0x01)); // line 0 granted -> allowed

    // Lines 1-4 (96-99) are blocked (selection cancelled) unless their bit is in the granted mask.
    REQUIRE(mth::train_destination_blocked(0x60, 0x01));       // line 1 ungranted (only line 0) -> blocked
    REQUIRE_FALSE(mth::train_destination_blocked(0x60, 0x02)); // line 1 granted -> allowed

    // Line 4 (99, Coltrane) is hardcoded always-shown in the menu, so the gate MUST still block it.
    REQUIRE(mth::train_destination_blocked(0x63, 0x00));       // line 4 ungranted -> blocked
    REQUIRE_FALSE(mth::train_destination_blocked(0x63, 0x10)); // line 4 granted -> allowed

    // Non-ticket codes are never blocked (100 = Exit/cancel sentinel; a normal item).
    REQUIRE_FALSE(mth::train_destination_blocked(100, 0x00));
    REQUIRE_FALSE(mth::train_destination_blocked(0x5e, 0x00)); // 94 generic pass, not a destination line
    REQUIRE_FALSE(mth::train_destination_blocked(41, 0x00));
}

TEST_CASE("maintained_vial_held preserves the missing flask count across a capacity change", "[upgrade]")
{
    REQUIRE(mth::maintained_vial_held(3, 3, 1) == 1); // full 3/3 -> new cap 1 stays full 1/1
    REQUIRE(mth::maintained_vial_held(3, 3, 5) == 5); // full 3/3 -> cap 5 fills to 5/5
    REQUIRE(mth::maintained_vial_held(2, 0, 3) == 1); // empty 0/2, +1 cap -> 1/3 (missing 2 preserved)
    REQUIRE(mth::maintained_vial_held(1, 1, 1) == 1); // resend, no change
    REQUIRE(mth::maintained_vial_held(3, 1, 0) == 0); // cap drops to 0 -> clamped, no negative
    REQUIRE(mth::maintained_vial_held(0, 0, 4) == 4); // fresh file, cap 4 -> full 4/4
}
