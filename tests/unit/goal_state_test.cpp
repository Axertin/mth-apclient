#include <catch2/catch_test_macros.hpp>

#include "mth/core/goal_state.hpp"

TEST_CASE("finish goal keys only on the game-clear flag", "[goal]")
{
    // counts are irrelevant for the finish goal
    REQUIRE(mth::goal_met(mth::kGoalFinish, 99, 99, true, 0, 0));
    REQUIRE_FALSE(mth::goal_met(mth::kGoalFinish, 0, 0, false, 50, 50));
}

TEST_CASE("generators goal compares generator count to the threshold", "[goal]")
{
    REQUIRE_FALSE(mth::goal_met(mth::kGoalGenerators, 5, 99, false, 4, 0)); // below
    REQUIRE(mth::goal_met(mth::kGoalGenerators, 5, 99, false, 5, 0));       // exactly met
    REQUIRE(mth::goal_met(mth::kGoalGenerators, 5, 99, false, 7, 0));       // above
}

TEST_CASE("bosses goal compares boss count to the threshold", "[goal]")
{
    REQUIRE_FALSE(mth::goal_met(mth::kGoalBosses, 99, 10, false, 0, 9)); // below
    REQUIRE(mth::goal_met(mth::kGoalBosses, 99, 10, false, 0, 10));      // exactly met
    REQUIRE(mth::goal_met(mth::kGoalBosses, 99, 10, false, 0, 12));      // above
}

TEST_CASE("unknown goal_config falls back to finish", "[goal]")
{
    REQUIRE(mth::goal_met(7, 0, 0, true, 0, 0));
    REQUIRE_FALSE(mth::goal_met(7, 0, 0, false, 99, 99));
}
