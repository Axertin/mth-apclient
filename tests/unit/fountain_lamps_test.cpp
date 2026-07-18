#include <catch2/catch_test_macros.hpp>

#include "mth/core/fountain_lamps.hpp"

using mth::lit_mask_from_indices;

TEST_CASE("lit_mask_from_indices: empty -> 0", "[fountain]")
{
    REQUIRE(lit_mask_from_indices({}) == 0u);
}

TEST_CASE("lit_mask_from_indices: each index sets its single bit", "[fountain]")
{
    REQUIRE(lit_mask_from_indices({0}) == 0x01u);
    REQUIRE(lit_mask_from_indices({1}) == 0x02u);
    REQUIRE(lit_mask_from_indices({5}) == 0x20u);
}

TEST_CASE("lit_mask_from_indices: all six -> 0x3F", "[fountain]")
{
    REQUIRE(lit_mask_from_indices({0, 1, 2, 3, 4, 5}) == 0x3Fu);
}

TEST_CASE("lit_mask_from_indices: out-of-range ignored (prime, over, negative)", "[fountain]")
{
    REQUIRE(lit_mask_from_indices({6}) == 0u); // prime lamp index, never forced
    REQUIRE(lit_mask_from_indices({7}) == 0u);
    REQUIRE(lit_mask_from_indices({99}) == 0u);
    REQUIRE(lit_mask_from_indices({-1}) == 0u);
    REQUIRE(lit_mask_from_indices({2, 6, -3, 4}) == ((1u << 2) | (1u << 4)));
}

TEST_CASE("lit_mask_from_indices: duplicates idempotent", "[fountain]")
{
    REQUIRE(lit_mask_from_indices({3, 3, 3}) == (1u << 3));
}
