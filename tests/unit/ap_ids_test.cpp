#include <iterator>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/ap_ids.hpp"

TEST_CASE("ap_item_id and game_item_type round-trip", "[ap_ids]")
{
    REQUIRE(mth::ap_item_id(0) == mth::kItemBase);
    REQUIRE(mth::ap_item_id(42) == mth::kItemBase + 42);
    REQUIRE(mth::game_item_type(mth::ap_item_id(42)) == 42);
    REQUIRE(mth::game_item_type(mth::kItemBase) == 0);
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

TEST_CASE("Legovich weapon-upgrade shop slots are locations 174-177", "[legovich]")
{
    REQUIRE(std::size(mth::kLegovichLocations) == 4);
    REQUIRE(mth::kLegovichLocations[0] == 174);
    REQUIRE(mth::kLegovichLocations[3] == 177);
}

TEST_CASE("legovich_arena_should_open gates Armand on all weapon slots bought", "[legovich]")
{
    auto all_ap = [](int) { return true; };

    // All Legovich slots are AP locations but none bought yet -> keep the arena sealed.
    REQUIRE_FALSE(mth::legovich_arena_should_open(all_ap, [](int) { return false; }));

    // A partial buy (only 174 and 175 checked) still seals -- this is the bug: the 2nd buy must NOT arm Armand.
    auto partial = [](int loc) { return loc == 174 || loc == 175; };
    REQUIRE_FALSE(mth::legovich_arena_should_open(all_ap, partial));

    // Every weapon slot bought -> open the arena (the intended finale).
    REQUIRE(mth::legovich_arena_should_open(all_ap, [](int) { return true; }));

    // Legovich not randomized (no slot is an AP location) -> defer to vanilla, allow the open.
    auto none_ap = [](int) { return false; };
    REQUIRE(mth::legovich_arena_should_open(none_ap, [](int) { return false; }));
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
