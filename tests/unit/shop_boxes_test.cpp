#include <catch2/catch_test_macros.hpp>

#include "mth/core/shop_boxes.hpp"

// The hook site rejects an implausible count at compile time, so keep it constexpr-callable.
static_assert(mth::shop_box_walk_count(mth::kMaxShopBoxes + 1) == 0);

TEST_CASE("shop_box_walk_count: a stocked shop walks every row", "[shop_boxes]")
{
    REQUIRE(mth::shop_box_walk_count(1) == 1);
    REQUIRE(mth::shop_box_walk_count(5) == 5);
    REQUIRE(mth::shop_box_walk_count(mth::kMaxShopBoxes) == mth::kMaxShopBoxes);
}

TEST_CASE("shop_box_walk_count: an empty row list walks nothing", "[shop_boxes]")
{
    REQUIRE(mth::shop_box_walk_count(0) == 0);
}

TEST_CASE("shop_box_walk_count: a negative row count walks nothing", "[shop_boxes]")
{
    REQUIRE(mth::shop_box_walk_count(-1) == 0);
    REQUIRE(mth::shop_box_walk_count(-0x1000000) == 0);
}

// An out-of-range count means the field is not a row count, so bail rather than clamp and walk anyway.
TEST_CASE("shop_box_walk_count: an implausible row count walks nothing", "[shop_boxes]")
{
    REQUIRE(mth::shop_box_walk_count(mth::kMaxShopBoxes + 1) == 0);
    REQUIRE(mth::shop_box_walk_count(0x1000000) == 0);
}
