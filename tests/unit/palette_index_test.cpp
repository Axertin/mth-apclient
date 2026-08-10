#include <catch2/catch_test_macros.hpp>

#include "mth/core/palette_index.hpp"

TEST_CASE("palette_target_index passes through an in-range index", "[palette]")
{
    REQUIRE(mth::palette_target_index(3, 16) == 3u);
    REQUIRE(mth::palette_target_index(0, 16) == 0u);
    REQUIRE(mth::palette_target_index(15, 16) == 15u);
}

TEST_CASE("palette_target_index treats a miss as index 0", "[palette]")
{
    // The engine's GetIndex answers 0 for a color that is not a palette member; a negative
    // answer from the mod API means the same thing and must land on the same entry.
    REQUIRE(mth::palette_target_index(-1, 16) == 0u);
    REQUIRE(mth::palette_target_index(-999, 16) == 0u);
}

TEST_CASE("palette_target_index rejects an index past the palette", "[palette]")
{
    // PaletteWriteIndex has no bounds check, so an out-of-range answer must not reach it.
    REQUIRE_FALSE(mth::palette_target_index(16, 16).has_value());
    REQUIRE_FALSE(mth::palette_target_index(17, 16).has_value());
    REQUIRE_FALSE(mth::palette_target_index(0, 0).has_value());
    REQUIRE_FALSE(mth::palette_target_index(-1, 0).has_value());
}
