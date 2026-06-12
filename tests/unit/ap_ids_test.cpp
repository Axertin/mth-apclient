#include <catch2/catch_test_macros.hpp>

#include "mth/core/ap_ids.hpp"

TEST_CASE("ap_item_id and game_item_type round-trip", "[ap_ids]")
{
    REQUIRE(mth::ap_item_id(0) == mth::kItemBase);
    REQUIRE(mth::ap_item_id(42) == mth::kItemBase + 42);
    REQUIRE(mth::game_item_type(mth::ap_item_id(42)) == 42);
    REQUIRE(mth::game_item_type(mth::kItemBase) == 0);
}

TEST_CASE("stat-cap item ids are distinct from game-item ids", "[ap_ids]")
{
    REQUIRE(mth::is_stat_cap_item(mth::kStatCapItemBase) == true);
    REQUIRE(mth::is_stat_cap_item(mth::kStatCapItemBase + 2) == true);
    REQUIRE(mth::is_stat_cap_item(mth::kStatCapItemBase + 3) == false); // only 3 stats
    REQUIRE(mth::is_stat_cap_item(mth::ap_item_id(42)) == false);       // a real game item
    REQUIRE(mth::stat_cap_item_stat(mth::kStatCapItemBase) == 0);
    REQUIRE(mth::stat_cap_item_stat(mth::kStatCapItemBase + 2) == 2);
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
