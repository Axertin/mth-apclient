#include <catch2/catch_test_macros.hpp>

#include "mth/core/session_policy.hpp"

TEST_CASE("nothing armed: enforcement follows AP authentication only", "[session_policy]")
{
    const mth::SessionPolicy p;
    REQUIRE_FALSE(p.enforce_modifiers(false));
    REQUIRE(p.enforce_modifiers(true));
    REQUIRE_FALSE(p.enforce_caps(false));
    REQUIRE(p.enforce_caps(true));
    REQUIRE_FALSE(p.enforce_abilities(false));
    REQUIRE(p.enforce_abilities(true));
    REQUIRE_FALSE(p.caps_fixed());
}

TEST_CASE("console arming enforces per feature without an AP session", "[session_policy]")
{
    mth::SessionPolicy mods;
    mods.arm_console_modifiers();
    REQUIRE(mods.enforce_modifiers(false));
    REQUIRE_FALSE(mods.enforce_caps(false));      // modifiers arming does not arm caps
    REQUIRE_FALSE(mods.enforce_abilities(false)); // nor abilities

    mth::SessionPolicy abil;
    abil.arm_console_abilities();
    REQUIRE(abil.enforce_abilities(false));
    REQUIRE_FALSE(abil.enforce_modifiers(false));
}

TEST_CASE("forced caps enforce without AP and disable AP recompute", "[session_policy]")
{
    mth::SessionPolicy p;
    p.arm_forced_caps();
    REQUIRE(p.enforce_caps(false));
    REQUIRE(p.caps_fixed());
    REQUIRE_FALSE(p.enforce_modifiers(false)); // caps arming does not arm modifiers
}
