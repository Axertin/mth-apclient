#include <catch2/catch_test_macros.hpp>

#include "mth/core/rando_bridge.hpp"

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
