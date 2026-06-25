#include <filesystem>

#include <catch2/catch_test_macros.hpp>

#include "mocks/fake_ap_link.hpp"
#include "mth/core/ap_save_state.hpp"
#include "mth/core/ap_state.hpp"
#include "mth/core/rando_bridge.hpp"

using mth::ap_loc_id;

namespace
{
// ApState is non-copyable/non-movable (mutex + atomic); populate in-place.
void connect_with(mth::ApState &s, std::vector<std::int64_t> missing)
{
    s.apply(mth::ApConnected{{}, "{}", 1, {}, std::move(missing)});
}
} // namespace

TEST_CASE("rando_bridge: valid location is sent once", "[mth][rando]")
{
    mth::test::FakeApLink link;
    link.connected = true;
    mth::ApState state;
    connect_with(state, {ap_loc_id(5), ap_loc_id(6)});
    mth::RandoBridge bridge(link, state);

    bridge.on_location_collected(5);
    REQUIRE(link.sent_locations == std::vector<std::int64_t>{ap_loc_id(5)});

    bridge.on_location_collected(5);
    REQUIRE(link.sent_locations.size() == 1);
}

TEST_CASE("rando_bridge: unknown location is dropped", "[mth][rando]")
{
    mth::test::FakeApLink link;
    mth::ApState state;
    connect_with(state, {ap_loc_id(5)});
    mth::RandoBridge bridge(link, state);

    bridge.on_location_collected(7);
    REQUIRE(link.sent_locations.empty());
}

TEST_CASE("rando_bridge: negative slot is ignored", "[mth][rando]")
{
    mth::test::FakeApLink link;
    mth::ApState state;
    connect_with(state, {ap_loc_id(0)});
    mth::RandoBridge bridge(link, state);

    bridge.on_location_collected(-1);
    REQUIRE(link.sent_locations.empty());
}

TEST_CASE("rando_bridge: persists checks and flushes the full set", "[mth][rando]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_bridge_flush.state";
    std::filesystem::remove(path);
    mth::ApSaveState save(path);

    mth::test::FakeApLink link;
    link.connected = true;
    mth::ApState state;
    connect_with(state, {ap_loc_id(5), ap_loc_id(9)});
    mth::RandoBridge bridge(link, state);
    bridge.attach_save_state(save);

    bridge.on_location_collected(5);
    REQUIRE(save.is_checked(5));
    REQUIRE(link.sent_locations == std::vector<std::int64_t>{ap_loc_id(5)});

    bridge.on_location_collected(9);
    link.sent_locations.clear();
    bridge.flush(); // resend the whole set
    REQUIRE(link.sent_locations == std::vector<std::int64_t>{ap_loc_id(5), ap_loc_id(9)});
}

TEST_CASE("rando_bridge: disconnected checks persist, flush on connect", "[mth][rando]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_bridge_offline.state";
    std::filesystem::remove(path);
    mth::ApSaveState save(path);

    mth::test::FakeApLink link;
    link.connected = false;
    mth::ApState state;
    connect_with(state, {ap_loc_id(5)});
    mth::RandoBridge bridge(link, state);
    bridge.attach_save_state(save);

    bridge.on_location_collected(5);
    REQUIRE(save.is_checked(5));
    REQUIRE(link.sent_locations.empty());

    link.connected = true;
    bridge.flush();
    REQUIRE(link.sent_locations == std::vector<std::int64_t>{ap_loc_id(5)});
}

TEST_CASE("rando_bridge: is_ap_location reflects the server set", "[mth][rando]")
{
    mth::test::FakeApLink link;
    mth::ApState state;
    connect_with(state, {ap_loc_id(5)});
    mth::RandoBridge bridge(link, state);
    REQUIRE(bridge.is_ap_location(5));
    REQUIRE_FALSE(bridge.is_ap_location(6));
    REQUIRE_FALSE(bridge.is_ap_location(-1));
}

TEST_CASE("rando_bridge: double-collect of the same location sends once", "[mth][rando]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_bridge_dup.state";
    std::filesystem::remove(path);
    mth::ApSaveState save(path);

    mth::test::FakeApLink link;
    link.connected = true;
    mth::ApState state;
    connect_with(state, {ap_loc_id(5)});
    mth::RandoBridge bridge(link, state);
    bridge.attach_save_state(save);

    bridge.on_location_collected(5);
    bridge.on_location_collected(5);
    REQUIRE(link.sent_locations == std::vector<std::int64_t>{ap_loc_id(5)});
}

TEST_CASE("rando_bridge: is_checked reflects collected locations (durable)", "[mth][rando]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_bridge_ischecked.state";
    std::filesystem::remove(path);
    mth::ApSaveState save(path);

    mth::test::FakeApLink link;
    link.connected = true;
    mth::ApState state;
    connect_with(state, {ap_loc_id(5), ap_loc_id(9)});
    mth::RandoBridge bridge(link, state);
    bridge.attach_save_state(save);

    REQUIRE_FALSE(bridge.is_checked(5));
    bridge.on_location_collected(5);
    REQUIRE(bridge.is_checked(5));
    REQUIRE_FALSE(bridge.is_checked(9));
    REQUIRE_FALSE(bridge.is_checked(-1));
}

TEST_CASE("rando_bridge: is_checked uses the session set before a save attaches", "[mth][rando]")
{
    mth::test::FakeApLink link;
    link.connected = true;
    mth::ApState state;
    connect_with(state, {ap_loc_id(5)});
    mth::RandoBridge bridge(link, state); // no attach_save_state

    REQUIRE_FALSE(bridge.is_checked(5));
    bridge.on_location_collected(5);
    REQUIRE(bridge.is_checked(5));
}

TEST_CASE("rando_bridge: send_goal sends the AP goal once when connected", "[mth][rando]")
{
    mth::test::FakeApLink link;
    link.connected = true;
    mth::ApState state;
    connect_with(state, {}); // authenticated, no locations needed for the goal
    mth::RandoBridge bridge(link, state);

    bridge.send_goal();
    bridge.send_goal(); // one-shot: GigaLionel hits the death funnels 3x per kill
    REQUIRE(link.goal_calls == 1);
}

TEST_CASE("rando_bridge: send_goal is a no-op when not authenticated", "[mth][rando]")
{
    mth::test::FakeApLink link;
    mth::ApState state; // never connected
    mth::RandoBridge bridge(link, state);

    bridge.send_goal();
    REQUIRE(link.goal_calls == 0);
}
