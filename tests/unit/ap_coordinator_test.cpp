#include <catch2/catch_test_macros.hpp>

#include "mocks/fake_ap_link.hpp"
#include "mth/core/ap_coordinator.hpp"
#include "mth/core/ap_state.hpp"

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
