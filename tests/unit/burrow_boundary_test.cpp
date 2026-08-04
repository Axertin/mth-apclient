#include <catch2/catch_test_macros.hpp>

#include "mth/core/burrow_boundary.hpp"

using mth::BoundaryAction;
using mth::BurrowBoundaryGate;

namespace
{
// Hold one water reading until the gate either acts or has had a full confirmation window to do so.
BoundaryAction settle(BurrowBoundaryGate &g, bool deep, bool burrow_blocked, bool swim_blocked)
{
    BoundaryAction act = BoundaryAction::None;
    for (int i = 0; i < BurrowBoundaryGate::kWaterConfirmTicks + 1 && act == BoundaryAction::None; ++i)
        act = g.observe(deep, burrow_blocked, swim_blocked);
    return act;
}
} // namespace

TEST_CASE("burrow boundary: burrowing into deep water without Swim falls in", "[ability][burrow]")
{
    BurrowBoundaryGate gate;
    gate.arm(false); // committed a land burrow

    REQUIRE(settle(gate, true, /*burrow_blocked=*/false, /*swim_blocked=*/true) == BoundaryAction::FallIn);
}

TEST_CASE("burrow boundary: swimming onto land without Burrow surfaces the player", "[ability][burrow]")
{
    BurrowBoundaryGate gate;
    gate.arm(true); // committed a swim

    REQUIRE(settle(gate, false, /*burrow_blocked=*/true, /*swim_blocked=*/false) == BoundaryAction::Emerge);
}

// Owning the ability the player crosses into is the ordinary case, and it must stay silent - including on the
// crossing AFTER it, which is only correct if the gate tracked the mode change.
TEST_CASE("burrow boundary: a granted crossing is silent and updates the tracked mode", "[ability][burrow]")
{
    BurrowBoundaryGate gate;
    gate.arm(false);

    REQUIRE(settle(gate, true, false, /*swim_blocked=*/false) == BoundaryAction::None); // has Swim: now swimming
    // Back onto land, this time without Burrow: the gate must know it is currently a SWIM to catch this.
    REQUIRE(settle(gate, false, /*burrow_blocked=*/true, false) == BoundaryAction::Emerge);
}

TEST_CASE("burrow boundary: no crossing means no action", "[ability][burrow]")
{
    BurrowBoundaryGate gate;
    gate.arm(false);

    REQUIRE(settle(gate, false, true, true) == BoundaryAction::None);
    REQUIRE(settle(gate, false, true, true) == BoundaryAction::None);
}

TEST_CASE("burrow boundary: an unarmed gate never acts", "[ability][burrow]")
{
    BurrowBoundaryGate gate;

    REQUIRE(settle(gate, true, true, true) == BoundaryAction::None);
    REQUIRE(settle(gate, false, true, true) == BoundaryAction::None);
}

TEST_CASE("burrow boundary: disarming stops the gate acting on a later crossing", "[ability][burrow]")
{
    BurrowBoundaryGate gate;
    gate.arm(false);
    gate.disarm();

    REQUIRE(settle(gate, true, true, /*swim_blocked=*/true) == BoundaryAction::None);
}

TEST_CASE("burrow boundary: acting disarms so it does not repeat every tick", "[ability][burrow]")
{
    BurrowBoundaryGate gate;
    gate.arm(false);

    REQUIRE(settle(gate, true, false, true) == BoundaryAction::FallIn);
    REQUIRE(settle(gate, true, false, true) == BoundaryAction::None);
    REQUIRE(settle(gate, true, false, true) == BoundaryAction::None);
}

// Acting consumes the arm, so one flicker of the raw reading would punish a player walking a beach.
TEST_CASE("burrow boundary: a single spurious deep reading does not act", "[ability][burrow]")
{
    BurrowBoundaryGate gate;
    gate.arm(false);

    REQUIRE(gate.observe(true, false, /*swim_blocked=*/true) == BoundaryAction::None);  // one-frame blip
    REQUIRE(gate.observe(false, false, /*swim_blocked=*/true) == BoundaryAction::None); // back on land
    REQUIRE(gate.observe(false, false, /*swim_blocked=*/true) == BoundaryAction::None);
}

// A gap in the water reading (null WaterListener during a room transition) must not be bridgeable: a partial
// confirmation that survives the gap would let 9 deep polls plus one more act as if the reading held.
TEST_CASE("burrow boundary: an unavailable reading restarts the confirmation", "[ability][burrow]")
{
    BurrowBoundaryGate gate;
    gate.arm(false);

    for (int i = 0; i < BurrowBoundaryGate::kWaterConfirmTicks - 1; ++i)
        REQUIRE(gate.observe(true, false, true) == BoundaryAction::None);
    gate.reading_unavailable();

    REQUIRE(gate.observe(true, false, true) == BoundaryAction::None);
}

// A burrow requested by another entity's state machine (a room transition) is applied a frame late, so a
// single non-burrow reading must not drop the arm. Damage and death hold that reading and still disarm.
TEST_CASE("burrow boundary: a one-frame gap in the burrow state keeps the arm", "[ability][burrow]")
{
    BurrowBoundaryGate gate;
    gate.arm(false);

    gate.observe_burrowing(false);
    REQUIRE(gate.armed());
}

TEST_CASE("burrow boundary: a sustained non-burrow state drops the arm", "[ability][burrow]")
{
    BurrowBoundaryGate gate;
    gate.arm(false);

    for (int i = 0; i < BurrowBoundaryGate::kBurrowConfirmGraceTicks; ++i)
        gate.observe_burrowing(false);

    REQUIRE_FALSE(gate.armed());
}
