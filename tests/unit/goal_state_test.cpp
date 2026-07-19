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

TEST_CASE("broken_generator_mask folds indices into the bitfield mask", "[goal]")
{
    REQUIRE(mth::broken_generator_mask({1, 2, 3}) == 0b1110u);
    REQUIRE(mth::broken_generator_mask({0}) == 0b1u);
    REQUIRE(mth::broken_generator_mask({}) == 0u);
    REQUIRE(mth::broken_generator_mask({2, 2}) == 0b100u); // duplicates idempotent
    // Out of range either way is ignored rather than shifting past the bitfield width.
    REQUIRE(mth::broken_generator_mask({-1, 64, 999}) == 0u);
    REQUIRE(mth::broken_generator_mask({63}) == (std::uint64_t{1} << 63));
}

TEST_CASE("generators_done counts only generators the seed started broken (#141)", "[goal]")
{
    // Real slot_data shape: lit_generators [0,4,5] start repaired, broken_generators [1,2,3] must be fixed.
    const std::uint64_t broken = mth::broken_generator_mask({1, 2, 3});

    REQUIRE(mth::generators_done(0b110001u, broken) == 0); // fresh save: pre-lit ones are flagged, none count
    REQUIRE(mth::generators_done(0b110011u, broken) == 1);
    REQUIRE(mth::generators_done(0b110111u, broken) == 2);
    REQUIRE(mth::generators_done(0b111111u, broken) == 3);
    // The bug this fixes: unmasked, a fresh save already reads 3 of 3 and fires the goal immediately.
    REQUIRE(mth::generators_done(0b110001u, mth::kAllGeneratorsMask) == 3);
    REQUIRE(mth::goal_met(mth::kGoalGenerators, 3, 0, false, mth::generators_done(0b110001u, mth::kAllGeneratorsMask), 0));
    REQUIRE_FALSE(mth::goal_met(mth::kGoalGenerators, 3, 0, false, mth::generators_done(0b110001u, broken), 0));
}

TEST_CASE("an absent broken_generators key counts every generator", "[goal]")
{
    REQUIRE(mth::generators_done(0b110001u, mth::kAllGeneratorsMask) == 3);
    REQUIRE(mth::generators_done(0u, mth::kAllGeneratorsMask) == 0);
}
