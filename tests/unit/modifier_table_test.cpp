#include <catch2/catch_test_macros.hpp>

#include "mth/core/data/modifier_table.hpp"

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

TEST_CASE("is_ap_denied covers the hostile modifiers", "[modifiers]")
{
    REQUIRE(mth::is_ap_denied(0));   // High Jump: vertical reach
    REQUIRE(mth::is_ap_denied(2));   // Infinite Jump
    REQUIRE(mth::is_ap_denied(5));   // Floatier Jumps
    REQUIRE(mth::is_ap_denied(17));  // Grapple Mode
    REQUIRE(mth::is_ap_denied(21));  // Walk On Pits
    REQUIRE(mth::is_ap_denied(19));  // Hedgehog grants a trinket
    REQUIRE(mth::is_ap_denied(54));  // EarlyWeapons grants weapons
    REQUIRE(mth::is_ap_denied(70));  // SidearmRandom
    REQUIRE(mth::is_ap_denied(95));  // AllWeapons
    REQUIRE(mth::is_ap_denied(123)); // the game's own warp shuffle
    REQUIRE(mth::is_ap_denied(126)); // custom-game-mode bit; skips the starting-kit seed
    REQUIRE(mth::is_ap_denied(140)); // StartTrainPassMax
    REQUIRE(mth::is_ap_denied(48));  // NoUnderlab
    REQUIRE(mth::is_ap_denied(107)); // NoBones
    REQUIRE(mth::is_ap_denied(119)); // NoPawnShop
    REQUIRE(mth::is_ap_denied(253)); // ComboBacker
}

TEST_CASE("is_ap_denied leaves difficulty and cosmetic modifiers alone", "[modifiers]")
{
    REQUIRE_FALSE(mth::is_ap_denied(1));   // Auto Jump
    REQUIRE_FALSE(mth::is_ap_denied(8));   // Burrow Chain
    REQUIRE_FALSE(mth::is_ap_denied(9));   // Infinite Burrow
    REQUIRE_FALSE(mth::is_ap_denied(20));  // Walk On Spikes
    REQUIRE_FALSE(mth::is_ap_denied(31));  // InstaKillPlayer
    REQUIRE_FALSE(mth::is_ap_denied(62));  // SuddenDeath
    REQUIRE_FALSE(mth::is_ap_denied(102)); // LevelingFast50, part of the AP force-on baseline
    REQUIRE_FALSE(mth::is_ap_denied(121)); // WarpHome, likewise
    REQUIRE_FALSE(mth::is_ap_denied(186)); // CloakColor1
    REQUIRE_FALSE(mth::is_ap_denied(213)); // CustomFlower
    REQUIRE_FALSE(mth::is_ap_denied(-1));  // invalid indices are never blocked
    REQUIRE_FALSE(mth::is_ap_denied(254));
}

TEST_CASE("is_ap_denied range boundaries are fence-post correct", "[modifiers]")
{
    // Edges around every denied range so an off-by-one is caught.
    REQUIRE(mth::is_ap_denied(7));         // last of the speed/float block
    REQUIRE_FALSE(mth::is_ap_denied(16));  // Vampire Vault, just below Grapple Mode
    REQUIRE_FALSE(mth::is_ap_denied(18));  // Dumb Throw, just above it
    REQUIRE_FALSE(mth::is_ap_denied(71));  // between the sidearm rerolls
    REQUIRE_FALSE(mth::is_ap_denied(73));  // above them
    REQUIRE(mth::is_ap_denied(89));        // first All*
    REQUIRE(mth::is_ap_denied(98));        // last All*
    REQUIRE_FALSE(mth::is_ap_denied(99));  // LevelingDeathOne
    REQUIRE_FALSE(mth::is_ap_denied(115)); // ShopPrice
    REQUIRE(mth::is_ap_denied(116));       // first shop removal
    REQUIRE(mth::is_ap_denied(120));       // last shop removal
    REQUIRE(mth::is_ap_denied(122));       // first randomizer flag
    REQUIRE(mth::is_ap_denied(125));       // last randomizer flag
    REQUIRE_FALSE(mth::is_ap_denied(127)); // StartProgBoat is a no-op
    REQUIRE(mth::is_ap_denied(128));       // first Start* grant
    REQUIRE(mth::is_ap_denied(172));       // last Start* grant
    REQUIRE_FALSE(mth::is_ap_denied(173)); // QuickLocks
    REQUIRE(mth::is_ap_denied(214));       // first combo
    REQUIRE(mth::is_ap_denied(253));       // last combo
}
