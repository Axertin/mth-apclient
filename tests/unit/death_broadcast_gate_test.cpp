#include <catch2/catch_test_macros.hpp>

#include "mth/core/death_broadcast_gate.hpp"

// observe(dying, alive): `dying` is the death-state trigger (the guard byte, which pulses during the death
// sequence); `alive` is a stable "truly alive" signal (health > 0). Broadcast fires once per death and
// re-arms only on a real respawn, so the pulsing `dying` byte cannot re-fire.

TEST_CASE("death_broadcast_gate: one broadcast per death despite a flickering dying byte", "[mth][death]")
{
    mth::DeathBroadcastGate g;
    REQUIRE_FALSE(g.observe(false, true));  // alive
    REQUIRE(g.observe(true, false));        // death -> fire once
    REQUIRE_FALSE(g.observe(false, false)); // guard pulses off but still dead (health 0) -> no re-arm
    REQUIRE_FALSE(g.observe(true, false));  // guard pulses back on -> latched, no re-fire
    REQUIRE_FALSE(g.observe(false, false));
    REQUIRE_FALSE(g.observe(true, false));
    REQUIRE_FALSE(g.observe(false, true)); // respawn (not dying + health > 0) -> re-arm
    REQUIRE(g.observe(true, false));       // the next death fires
}

TEST_CASE("death_broadcast_gate: does not re-arm mid-death even if health reads alive", "[mth][death]")
{
    mth::DeathBroadcastGate g;
    REQUIRE(g.observe(true, false));       // first observed state is dying -> fire
    REQUIRE_FALSE(g.observe(true, false)); // sustained dying -> no re-fire
    REQUIRE_FALSE(g.observe(true, true));  // health flickers >0 WHILE dying -> still no re-arm/re-fire
    REQUIRE_FALSE(g.observe(true, false));
}

TEST_CASE("death_broadcast_gate: arm_suppress consumes the next (self-applied) death", "[mth][death]")
{
    mth::DeathBroadcastGate g;
    REQUIRE_FALSE(g.observe(false, true));
    g.arm_suppress();
    REQUIRE_FALSE(g.observe(true, false));  // inbound death we applied -> suppressed, not echoed
    REQUIRE_FALSE(g.observe(false, false)); // still dead
    REQUIRE_FALSE(g.observe(false, true));  // respawn -> re-arm
    REQUIRE(g.observe(true, false));        // a later genuine death still fires
}
