#include <catch2/catch_test_macros.hpp>

#include "mocks/fake_ap_link.hpp"
#include "mth/core/ap/ap_coordinator.hpp"
#include "mth/core/ap/ap_state.hpp"

TEST_CASE("ap_coordinator: tick drains link events into state", "[mth][ap_coordinator]")
{
    mth::test::FakeApLink link;
    mth::ApState state;
    mth::ApCoordinator coord(link, state);

    link.pending.push_back(mth::ApConnected{{}, "{}", 2, {5}, {6}});
    link.pending.push_back(mth::ApItemReceived{{777, 0, 2, 1}});

    coord.tick();

    REQUIRE(state.authenticated());
    REQUIRE(state.player_slot() == 2);
    REQUIRE(state.is_valid_location(5));
    REQUIRE(state.received_items().size() == 1);
    REQUIRE(link.pending.empty());
}

TEST_CASE("ap_coordinator: tick with no events is a no-op", "[mth][ap_coordinator]")
{
    mth::test::FakeApLink link;
    mth::ApState state;
    mth::ApCoordinator coord(link, state);

    coord.tick();
    REQUIRE_FALSE(state.authenticated());
    REQUIRE(state.status() == "Idle");
}

TEST_CASE("ap_coordinator: on_death called when ApDeathReceived event drained", "[mth][ap_coordinator]")
{
    mth::test::FakeApLink link;
    mth::ApState state;
    bool death_called = false;
    mth::ApCoordinator coord(link, state, [&death_called] { death_called = true; });

    link.pending.push_back(mth::ApDeathReceived{"a rival player died"});

    coord.tick();

    REQUIRE(death_called);
    REQUIRE(link.pending.empty());
}

TEST_CASE("ap_coordinator: on_broadcast forwards segments from ApPrintBroadcast", "[mth][ap_coordinator]")
{
    mth::test::FakeApLink link;
    mth::ApState state;
    std::vector<mth::BannerSegment> got;
    mth::ApCoordinator coord(link, state, {}, [&got](const std::vector<mth::BannerSegment> &s) { got = s; });

    link.pending.push_back(mth::ApPrintBroadcast{{{"you got the thing", 0xFFFFFFFFu}}});

    coord.tick();

    REQUIRE(got.size() == 1);
    REQUIRE(got[0].text == "you got the thing");
    REQUIRE(link.pending.empty());
}

TEST_CASE("ap_coordinator: on_session_reset fires on ApConnected", "[mth][ap_coordinator]")
{
    mth::test::FakeApLink link;
    mth::ApState state;
    int reset_calls = 0;
    mth::ApCoordinator coord(link, state, {}, {}, {}, [&reset_calls] { ++reset_calls; });

    link.pending.push_back(mth::ApConnected{{}, "{}", 2, {5}, {6}});

    coord.tick();

    REQUIRE(reset_calls == 1);
}

TEST_CASE("ap_coordinator: on_session_reset fires on ApDisconnected", "[mth][ap_coordinator]")
{
    mth::test::FakeApLink link;
    mth::ApState state;
    int reset_calls = 0;
    mth::ApCoordinator coord(link, state, {}, {}, {}, [&reset_calls] { ++reset_calls; });

    link.pending.push_back(mth::ApDisconnected{});

    coord.tick();

    REQUIRE(reset_calls == 1);
}

TEST_CASE("ap_coordinator: on_session_reset does not fire on unrelated events", "[mth][ap_coordinator]")
{
    mth::test::FakeApLink link;
    mth::ApState state;
    int reset_calls = 0;
    mth::ApCoordinator coord(link, state, {}, {}, {}, [&reset_calls] { ++reset_calls; });

    link.pending.push_back(mth::ApItemReceived{{777, 0, 2, 1}});
    link.pending.push_back(mth::ApStatusChanged{"hi"});

    coord.tick();

    REQUIRE(reset_calls == 0);
}
