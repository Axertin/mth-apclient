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

TEST_CASE("broken_generator_mask maps slot_data indices onto SaveSlot 0x290 bits", "[goal]")
{
    // slot_data generator index != 0x290 bit position. The game's BossComponent::SetGeneratorFixed marks
    // bit {bayou/Noxs=0 -> 2, crypt/Queensbury=1 -> 1, septemburg=2 -> 3, boneBeach=3 -> 4, coltrane=4 -> 5,
    // astral=5 -> 6}, so the mask must be built in 0x290-bit space, not index space (#146).
    REQUIRE(mth::broken_generator_mask({0}) == (std::uint64_t{1} << 2)); // Noxs Bayou -> bit 2, NOT bit 0
    REQUIRE(mth::broken_generator_mask({1}) == (std::uint64_t{1} << 1)); // Queensbury Crypt -> bit 1
    REQUIRE(mth::broken_generator_mask({5}) == (std::uint64_t{1} << 6)); // Astral Orrery -> bit 6
    REQUIRE(mth::broken_generator_mask({0, 1, 2}) == 0b1110u);           // bits {2,1,3}
    REQUIRE(mth::broken_generator_mask({}) == 0u);
    REQUIRE(mth::broken_generator_mask({0, 0}) == (std::uint64_t{1} << 2)); // duplicates idempotent
    // Only the six real generators exist; anything else is ignored rather than shifted in.
    REQUIRE(mth::broken_generator_mask({-1, 6, 64, 999}) == 0u);
}

TEST_CASE("generators_done counts only generators the seed started broken (#141)", "[goal]")
{
    // Real slot_data shape: broken_generators {Noxs=0, Queensbury=1, Septemburg=2} must be fixed; the other
    // three start pre-repaired. Everything below is in SaveSlot 0x290 bit space.
    const std::uint64_t broken = mth::broken_generator_mask({0, 1, 2}); // bits {2,1,3}
    const std::uint64_t prelit = mth::broken_generator_mask({3, 4, 5}); // bits {4,5,6}

    REQUIRE(mth::generators_done(prelit, broken) == 0);                           // fresh save: pre-lit ones are flagged, none count
    REQUIRE(mth::generators_done(prelit | (std::uint64_t{1} << 2), broken) == 1); // repair Noxs
    REQUIRE(mth::generators_done(prelit | broken, broken) == 3);                  // repair all three
    // The bug #141 fixed: unmasked, the pre-lit trio alone already reads 3 and fires the goal immediately.
    REQUIRE(mth::generators_done(prelit, mth::kAllGeneratorsMask) == 3);
    REQUIRE(mth::goal_met(mth::kGoalGenerators, 3, 0, false, mth::generators_done(prelit, mth::kAllGeneratorsMask), 0));
    REQUIRE_FALSE(mth::goal_met(mth::kGoalGenerators, 3, 0, false, mth::generators_done(prelit, broken), 0));
}

TEST_CASE("a goal set including Noxs Bayou completes once all its generators are fixed (#146)", "[goal]")
{
    // #146: a 3-generator goal that includes Noxs Bayou (index 0) never completed, because the mask put its
    // bit at position 0 -- a bit BossComponent::SetGeneratorFixed never sets (it uses bit 2 for bayou).
    const std::uint64_t broken = mth::broken_generator_mask({0, 1, 2});
    const std::uint64_t all_three_fixed = (std::uint64_t{1} << 2) | (std::uint64_t{1} << 1) | (std::uint64_t{1} << 3);
    REQUIRE(mth::generators_done(all_three_fixed, broken) == 3);
    REQUIRE(mth::goal_met(mth::kGoalGenerators, 3, 0, false, mth::generators_done(all_three_fixed, broken), 0));
}

TEST_CASE("an absent broken_generators key counts every generator", "[goal]")
{
    REQUIRE(mth::generators_done(0b110001u, mth::kAllGeneratorsMask) == 3);
    REQUIRE(mth::generators_done(0u, mth::kAllGeneratorsMask) == 0);
}
