#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "mth/core/trap_state.hpp"

TEST_CASE("TrapState: arming makes a trap active", "[trap]")
{
    mth::TrapState s;
    s.arm(204, 30.0f);
    REQUIRE(s.active() == std::vector<int>{204});
    REQUIRE(s.size() == 1);
    REQUIRE_FALSE(s.empty());
}

TEST_CASE("TrapState: a partial advance does not expire a trap", "[trap]")
{
    mth::TrapState s;
    s.arm(204, 30.0f);
    REQUIRE(s.advance(10.0f).empty());
    REQUIRE(s.active() == std::vector<int>{204});
}

TEST_CASE("TrapState: advancing past the duration expires it once", "[trap]")
{
    mth::TrapState s;
    s.arm(204, 30.0f);
    REQUIRE(s.advance(30.0f) == std::vector<int>{204});
    REQUIRE(s.empty());
    REQUIRE(s.advance(30.0f).empty()); // already gone, never reported twice
}

TEST_CASE("TrapState: an overshooting delta still expires exactly once", "[trap]")
{
    mth::TrapState s;
    s.arm(204, 1.0f);
    REQUIRE(s.advance(9999.0f) == std::vector<int>{204});
    REQUIRE(s.empty());
}

TEST_CASE("TrapState: re-arming refreshes and never shortens", "[trap]")
{
    mth::TrapState s;
    s.arm(204, 30.0f);
    s.advance(25.0f);  // 5s left
    s.arm(204, 30.0f); // refreshed to 30
    REQUIRE(s.size() == 1);
    REQUIRE(s.advance(10.0f).empty());

    s.arm(204, 2.0f); // shorter than what remains: must not cut it short
    REQUIRE(s.advance(10.0f).empty());
}

TEST_CASE("TrapState: traps stack and expire independently in order", "[trap]")
{
    mth::TrapState s;
    s.arm(204, 10.0f);
    s.arm(197, 20.0f);
    s.arm(202, 30.0f);
    REQUIRE(s.active() == std::vector<int>{204, 197, 202});

    REQUIRE(s.advance(10.0f) == std::vector<int>{204});
    REQUIRE(s.active() == std::vector<int>{197, 202});
    REQUIRE(s.advance(10.0f) == std::vector<int>{197});
    REQUIRE(s.advance(10.0f) == std::vector<int>{202});
    REQUIRE(s.empty());
}

TEST_CASE("TrapState: two traps expiring on the same tick both report", "[trap]")
{
    mth::TrapState s;
    s.arm(204, 5.0f);
    s.arm(197, 5.0f);
    REQUIRE(s.advance(5.0f) == std::vector<int>{204, 197});
}

TEST_CASE("TrapState: a non-positive delta ages nothing", "[trap]")
{
    mth::TrapState s;
    s.arm(204, 30.0f);
    REQUIRE(s.advance(0.0f).empty());
    REQUIRE(s.advance(-5.0f).empty());
    REQUIRE(s.advance(30.0f) == std::vector<int>{204}); // full duration was preserved
}

TEST_CASE("TrapState: a non-positive duration is ignored", "[trap]")
{
    mth::TrapState s;
    s.arm(204, 0.0f);
    s.arm(205, -1.0f);
    REQUIRE(s.empty());
}

TEST_CASE("TrapState: clear returns what was running and empties", "[trap]")
{
    mth::TrapState s;
    s.arm(204, 30.0f);
    s.arm(197, 30.0f);
    REQUIRE(s.clear() == std::vector<int>{204, 197});
    REQUIRE(s.empty());
    REQUIRE(s.clear().empty());
}

TEST_CASE("TrapState: active_with_remaining carries the seconds left", "[trap]")
{
    mth::TrapState s;
    REQUIRE(s.active_with_remaining().empty());

    s.arm(204, 30.0f);
    s.arm(197, 20.0f);
    s.advance(5.0f);

    const std::vector<mth::ActiveTrap> running = s.active_with_remaining();
    REQUIRE(running.size() == 2);
    REQUIRE(running[0].index == 204);
    REQUIRE(running[0].remaining == Catch::Approx(25.0f));
    REQUIRE(running[1].index == 197);
    REQUIRE(running[1].remaining == Catch::Approx(15.0f));
}

TEST_CASE("TrapState: active_with_remaining matches active() and drops what expired", "[trap]")
{
    mth::TrapState s;
    s.arm(204, 10.0f);
    s.arm(197, 30.0f);
    REQUIRE(s.advance(10.0f) == std::vector<int>{204});

    const std::vector<mth::ActiveTrap> running = s.active_with_remaining();
    REQUIRE(running.size() == 1);
    REQUIRE(running[0].index == 197);
    REQUIRE(running[0].remaining == Catch::Approx(20.0f));

    std::vector<int> indices;
    for (const mth::ActiveTrap &t : running)
        indices.push_back(t.index);
    REQUIRE(indices == s.active());

    REQUIRE(s.clear() == std::vector<int>{197});
    REQUIRE(s.active_with_remaining().empty());
}
