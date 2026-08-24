#include <catch2/catch_test_macros.hpp>

#include "mth/core/data/game_layout.hpp"

TEST_CASE("status_lvl_widget_offset walks the pause panel array stride", "[statusmenu]")
{
    // Setup builds the three panels in one loop; the level label is the first pointer in each block.
    REQUIRE(mth::layout::status_lvl_widget_offset(0) == 0x128);
    REQUIRE(mth::layout::status_lvl_widget_offset(1) == 0x168);
    REQUIRE(mth::layout::status_lvl_widget_offset(2) == 0x1a8);
}

TEST_CASE("status_lvl_widget_offset rejects a row that is not a real stat", "[statusmenu]")
{
    REQUIRE(mth::layout::status_lvl_widget_offset(-1) < 0);
    REQUIRE(mth::layout::status_lvl_widget_offset(3) < 0);
}
