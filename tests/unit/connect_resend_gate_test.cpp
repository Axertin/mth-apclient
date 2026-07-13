#include <catch2/catch_test_macros.hpp>

#include "mth/core/connect_resend_gate.hpp"

TEST_CASE("connect_resend_gate: fires once per connection when connected+ready", "[mth][resend]")
{
    mth::ConnectResendGate g;
    REQUIRE(g.fire(true, true));       // first tick connected + ready -> fire
    REQUIRE_FALSE(g.fire(true, true)); // still same connection -> no re-fire
    REQUIRE_FALSE(g.fire(true, true));
}

TEST_CASE("connect_resend_gate: waits for inbound_ready", "[mth][resend]")
{
    mth::ConnectResendGate g;
    REQUIRE_FALSE(g.fire(true, false)); // connected but not ready yet
    REQUIRE_FALSE(g.fire(true, false));
    REQUIRE(g.fire(true, true)); // ready arrives (later tick) -> fire once
    REQUIRE_FALSE(g.fire(true, true));
}

TEST_CASE("connect_resend_gate: never fires while disconnected", "[mth][resend]")
{
    mth::ConnectResendGate g;
    REQUIRE_FALSE(g.fire(false, true));
    REQUIRE_FALSE(g.fire(false, false));
}

TEST_CASE("connect_resend_gate: re-arms and fires again on reconnect", "[mth][resend]")
{
    mth::ConnectResendGate g;
    REQUIRE(g.fire(true, true)); // first connection
    REQUIRE_FALSE(g.fire(true, true));
    REQUIRE_FALSE(g.fire(false, true)); // disconnect re-arms
    REQUIRE(g.fire(true, true));        // reconnect -> fire again
}
