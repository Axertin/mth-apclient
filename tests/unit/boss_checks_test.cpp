#include <catch2/catch_test_macros.hpp>

#include "mth/core/boss_checks.hpp"

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
