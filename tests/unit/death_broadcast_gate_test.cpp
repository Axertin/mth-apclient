#include <catch2/catch_test_macros.hpp>

#include "mth/core/death_broadcast_gate.hpp"

TEST_CASE("death_broadcast_gate: broadcasts a fresh death edge once", "[mth][death]")
{
    mth::DeathBroadcastGate g;
    REQUIRE_FALSE(g.observe(false)); // alive
    REQUIRE(g.observe(true));        // 0->1 death edge -> broadcast
    REQUIRE_FALSE(g.observe(true));  // still dying -> no re-broadcast
    REQUIRE_FALSE(g.observe(false)); // recovered
    REQUIRE(g.observe(true));        // a new death -> broadcast again
}

TEST_CASE("death_broadcast_gate: arm_suppress consumes the next death edge", "[mth][death]")
{
    mth::DeathBroadcastGate g;
    g.arm_suppress();               // an inbound death WE applied
    REQUIRE_FALSE(g.observe(true)); // its edge is suppressed, not echoed back
    REQUIRE_FALSE(g.observe(false));
    REQUIRE(g.observe(true)); // a later genuine death still broadcasts
}
