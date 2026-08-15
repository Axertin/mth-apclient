#include <string>

#include <catch2/catch_test_macros.hpp>

#include "mocks/fake_ap_link.hpp"
#include "mth/core/ap/ap_coordinator.hpp"
#include "mth/core/ap/ap_ids.hpp"
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
    mth::ApCoordinator coord(link, state, [&death_called](const std::string &, const std::string &) { death_called = true; });

    link.pending.push_back(mth::ApDeathReceived{"a rival", "a rival player died"});

    coord.tick();

    REQUIRE(death_called);
    REQUIRE(link.pending.empty());
}

TEST_CASE("ap_coordinator: on_death forwards the sender and cause for attribution", "[mth][ap_coordinator]")
{
    mth::test::FakeApLink link;
    mth::ApState state;
    std::string got_source;
    std::string got_cause;
    mth::ApCoordinator coord(link, state,
                             [&](const std::string &source, const std::string &cause)
                             {
                                 got_source = source;
                                 got_cause = cause;
                             });

    link.pending.push_back(mth::ApDeathReceived{"Skylar", "Skylar was crushed by a spike trap"});

    coord.tick();

    REQUIRE(got_source == "Skylar");
    REQUIRE(got_cause == "Skylar was crushed by a spike trap");
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

TEST_CASE("ap_coordinator: ApSessionEnded fires on_session_end before the new ApConnected is applied", "[mth][ap_coordinator]")
{
    mth::test::FakeApLink link;
    mth::ApState state;
    int slot_when_cleared = -99;
    int end_calls = 0;
    mth::ApCoordinator coord(link, state, {}, {}, {}, {},
                             [&]
                             {
                                 ++end_calls;
                                 slot_when_cleared = state.player_slot(); // must still be the OLD session's
                                 state.reset_session();
                             });

    // Session A, then the link reports a different seed/slot: the marker precedes B's ApConnected.
    link.pending.push_back(mth::ApConnected{{}, "{}", 1, {}, {mth::ap_loc_id(5)}});
    coord.tick();
    state.apply(mth::ApItemReceived{mth::ReceivedItem{42, 0, 1, 0}});
    REQUIRE(state.received_items().size() == 1);

    link.pending.push_back(mth::ApSessionEnded{});
    link.pending.push_back(mth::ApConnected{{}, "{}", 9, {}, {mth::ap_loc_id(6)}});
    link.pending.push_back(mth::ApItemReceived{mth::ReceivedItem{99, 0, 9, 0}});
    coord.tick();

    REQUIRE(end_calls == 1);
    REQUIRE(slot_when_cleared == 1); // cleared while A was still the applied identity
    // B's ApConnected and its items landed on TOP of the clear, not under it.
    REQUIRE(state.player_slot() == 9);
    REQUIRE(state.authenticated());
    REQUIRE(state.received_items().size() == 1);
    REQUIRE(state.received_items()[0].item_id == 99);
}

TEST_CASE("ap_coordinator: a reconnect with no ApSessionEnded keeps the session state", "[mth][ap_coordinator]")
{
    mth::test::FakeApLink link;
    mth::ApState state;
    int end_calls = 0;
    mth::ApCoordinator coord(link, state, {}, {}, {}, {}, [&] { ++end_calls; });

    link.pending.push_back(mth::ApConnected{{}, "{}", 1, {}, {mth::ap_loc_id(5)}});
    coord.tick();
    state.apply(mth::ApItemReceived{mth::ReceivedItem{42, 0, 1, 0}});

    // Same seed+slot: the link emits no marker, so nothing is torn down (#152).
    link.pending.push_back(mth::ApDisconnected{});
    link.pending.push_back(mth::ApConnected{{}, "{}", 1, {}, {mth::ap_loc_id(5)}});
    coord.tick();

    REQUIRE(end_calls == 0);
    REQUIRE(state.received_items().size() == 1);
    REQUIRE(state.last_item_index() == 0);
}
