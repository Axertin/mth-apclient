#include <catch2/catch_test_macros.hpp>

#include "mth/core/session_policy.hpp"

TEST_CASE("nothing armed: enforcement follows AP authentication only", "[session_policy]")
{
    const mth::SessionPolicy p;
    REQUIRE_FALSE(p.enforce_modifiers(false));
    REQUIRE(p.enforce_modifiers(true));
    REQUIRE_FALSE(p.enforce_caps(false));
    REQUIRE(p.enforce_caps(true));
    REQUIRE_FALSE(p.caps_fixed());
}

TEST_CASE("env/console arming enforces modifiers without an AP session", "[session_policy]")
{
    mth::SessionPolicy env;
    env.arm_env_modifiers();
    REQUIRE(env.enforce_modifiers(false));

    mth::SessionPolicy console;
    console.arm_console_modifiers();
    REQUIRE(console.enforce_modifiers(false));
    REQUIRE_FALSE(console.enforce_caps(false)); // modifiers arming does not arm caps
}

TEST_CASE("forced caps enforce without AP and disable AP recompute", "[session_policy]")
{
    mth::SessionPolicy p;
    p.arm_forced_caps();
    REQUIRE(p.enforce_caps(false));
    REQUIRE(p.caps_fixed());
    REQUIRE_FALSE(p.enforce_modifiers(false)); // caps arming does not arm modifiers
}
