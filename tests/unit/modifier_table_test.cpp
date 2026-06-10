#include <catch2/catch_test_macros.hpp>

#include "mth/core/modifier_table.hpp"

using mth::CheatClass;

TEST_CASE("class_of partitions the index space", "[modifiers]")
{
    REQUIRE(mth::class_of(-1) == CheatClass::Invalid);
    REQUIRE(mth::class_of(254) == CheatClass::Invalid);
    REQUIRE(mth::class_of(0) == CheatClass::Continuous);
    REQUIRE(mth::class_of(31) == CheatClass::Continuous);
    REQUIRE(mth::class_of(126) == CheatClass::Continuous); // StartNewGamePlus is a no-op -> CONT
    REQUIRE(mth::class_of(19) == CheatClass::Grant);       // Hedgehog grants a trinket
    REQUIRE(mth::class_of(54) == CheatClass::Grant);
    REQUIRE(mth::class_of(140) == CheatClass::Grant);
    REQUIRE(mth::class_of(123) == CheatClass::Randomizer);
    REQUIRE(mth::class_of(218) == CheatClass::Combo);
    REQUIRE(mth::class_of(253) == CheatClass::Combo);
}

TEST_CASE("class_of range boundaries are fence-post correct", "[modifiers]")
{
    // Edges around every range so an off-by-one in class_of is caught.
    REQUIRE(mth::class_of(18) == CheatClass::Continuous);  // before Grant-19
    REQUIRE(mth::class_of(20) == CheatClass::Continuous);  // after Grant-19
    REQUIRE(mth::class_of(53) == CheatClass::Continuous);  // before Grant-54
    REQUIRE(mth::class_of(55) == CheatClass::Continuous);  // after Grant-54
    REQUIRE(mth::class_of(121) == CheatClass::Continuous); // before Randomizer
    REQUIRE(mth::class_of(122) == CheatClass::Randomizer); // first Randomizer
    REQUIRE(mth::class_of(125) == CheatClass::Randomizer); // last Randomizer
    REQUIRE(mth::class_of(127) == CheatClass::Continuous); // 126/127 are no-ops -> CONT
    REQUIRE(mth::class_of(128) == CheatClass::Grant);      // first Grant range
    REQUIRE(mth::class_of(172) == CheatClass::Grant);      // last Grant range
    REQUIRE(mth::class_of(173) == CheatClass::Continuous); // after Grant range
    REQUIRE(mth::class_of(213) == CheatClass::Continuous); // before Combo
    REQUIRE(mth::class_of(214) == CheatClass::Combo);      // first Combo
}

TEST_CASE("is_safe is exactly the continuous set", "[modifiers]")
{
    REQUIRE(mth::is_safe(31));
    REQUIRE_FALSE(mth::is_safe(140)); // grant
    REQUIRE_FALSE(mth::is_safe(123)); // randomizer
    REQUIRE_FALSE(mth::is_safe(218)); // combo
}

TEST_CASE("cosmetic indices are a visual subset of CONT", "[modifiers]")
{
    REQUIRE(mth::is_cosmetic(186));       // CloakColor1
    REQUIRE(mth::is_cosmetic(213));       // CustomFlower
    REQUIRE(mth::is_safe(186));           // all cosmetics are continuous
    REQUIRE_FALSE(mth::is_cosmetic(31));  // gameplay continuous
    REQUIRE_FALSE(mth::is_cosmetic(140)); // grant is never cosmetic
}

TEST_CASE("is_gameplay = valid and not cosmetic", "[modifiers]")
{
    REQUIRE(mth::is_gameplay(31));
    REQUIRE(mth::is_gameplay(140));       // grants are gameplay (lockdown blocks them)
    REQUIRE_FALSE(mth::is_gameplay(186)); // cosmetic
    REQUIRE_FALSE(mth::is_gameplay(254)); // invalid
}
