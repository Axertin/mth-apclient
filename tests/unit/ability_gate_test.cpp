#include <catch2/catch_test_macros.hpp>

#include "mth/core/ability_gate.hpp"
#include "mth/core/ability_ids.hpp"

using mth::Ability;

TEST_CASE("ability item ids occupy the 3000 segment")
{
    REQUIRE(mth::kAbilityCount == 7);
    REQUIRE(mth::ability_item_id(Ability::Burrow) == 3000);
    REQUIRE(mth::ability_item_id(Ability::Train) == 3006);
    REQUIRE(mth::is_ability_item(3000));
    REQUIRE(mth::is_ability_item(3006));
    REQUIRE_FALSE(mth::is_ability_item(2999));
    REQUIRE_FALSE(mth::is_ability_item(3007));
    REQUIRE(mth::ability_from_item(3005) == Ability::Carry);
}

TEST_CASE("ability_from_name maps known names and rejects unknown")
{
    REQUIRE(mth::ability_from_name("burrow") == Ability::Burrow);
    REQUIRE(mth::ability_from_name("swim") == Ability::Swim);
    REQUIRE(mth::ability_from_name("rope") == Ability::RopeClimb);
    REQUIRE(mth::ability_from_name("puff") == Ability::BouncePuff);
    REQUIRE(mth::ability_from_name("spring") == Ability::BounceSpring);
    REQUIRE(mth::ability_from_name("carry") == Ability::Carry);
    REQUIRE(mth::ability_from_name("train") == Ability::Train);
    REQUIRE_FALSE(mth::ability_from_name("unknown").has_value());
    REQUIRE_FALSE(mth::ability_from_name("").has_value());
    REQUIRE_FALSE(mth::ability_from_name("Burrow").has_value()); // case-sensitive
}

TEST_CASE("AbilityGate blocks only randomized, ungranted abilities on the AP slot")
{
    mth::AbilityGate gate;
    gate.set_randomized(Ability::Burrow, true); // randomized this seed
    // Swim left non-randomized.

    auto q = [&](bool slot_is_ap, bool granted) { return mth::AbilityGate::GrantQuery{slot_is_ap, [granted](std::int64_t) { return granted; }}; };

    REQUIRE(gate.blocked(Ability::Burrow, q(true, false)));        // randomized, AP slot, not granted -> block
    REQUIRE_FALSE(gate.blocked(Ability::Burrow, q(true, true)));   // granted -> allow
    REQUIRE_FALSE(gate.blocked(Ability::Burrow, q(false, false))); // not the AP slot -> allow
    REQUIRE_FALSE(gate.blocked(Ability::Swim, q(true, false)));    // not randomized -> allow
}
