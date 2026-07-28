#include <catch2/catch_test_macros.hpp>

#include "mth/core/title_menu.hpp"

TEST_CASE("skip_gated_option passes non-zero indices through unchanged", "[title_menu]")
{
    REQUIRE(mth::skip_gated_option(0, 1) == 1);
    REQUIRE(mth::skip_gated_option(0, 2) == 2);
    REQUIRE(mth::skip_gated_option(2, 1) == 1);
}

TEST_CASE("skip_gated_option continues upward past the gated option", "[title_menu]")
{
    // Up from 1 lands directly on 0 (no game-side wrap); continue the same direction to 2.
    REQUIRE(mth::skip_gated_option(1, 0) == 2);
}

TEST_CASE("skip_gated_option continues downward past the gated option", "[title_menu]")
{
    // Down from 2 wraps to 0; continue the same direction to 1.
    REQUIRE(mth::skip_gated_option(2, 0) == 1);
}

TEST_CASE("skip_gated_option is stable when there was no movement", "[title_menu]")
{
    REQUIRE(mth::skip_gated_option(0, 0) == 1);
}
