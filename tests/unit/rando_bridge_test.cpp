#include <filesystem>
#include <set>

#include <catch2/catch_test_macros.hpp>

#include "mocks/fake_ap_link.hpp"
#include "mth/core/ap/ap_save_state.hpp"
#include "mth/core/ap/ap_state.hpp"
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

TEST_CASE("rando_bridge: reconcile_server_checked marks without sending", "[mth][rando]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_bridge_collect.state";
    std::filesystem::remove(path);
    mth::ApSaveState save(path);
    mth::test::FakeApLink link;
    link.connected = true;
    mth::ApState state;
    connect_with(state, {ap_loc_id(5), ap_loc_id(9)});
    mth::RandoBridge bridge(link, state);
    bridge.attach_save_state(save);

    REQUIRE(bridge.reconcile_server_checked(5)); // newly checked
    REQUIRE(bridge.is_checked(5));
    REQUIRE(save.is_checked(5));
    REQUIRE(link.sent_locations.empty()); // never sent to the server
}

TEST_CASE("rando_bridge: reconcile_server_checked dedups and rejects non-AP", "[mth][rando]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_bridge_collect_dup.state";
    std::filesystem::remove(path);
    mth::ApSaveState save(path);
    mth::test::FakeApLink link;
    mth::ApState state;
    connect_with(state, {ap_loc_id(5)});
    mth::RandoBridge bridge(link, state);
    bridge.attach_save_state(save);

    REQUIRE(bridge.reconcile_server_checked(5));       // first time
    REQUIRE_FALSE(bridge.reconcile_server_checked(5)); // already checked
    REQUIRE_FALSE(bridge.reconcile_server_checked(7)); // not a valid AP location
    REQUIRE_FALSE(bridge.reconcile_server_checked(-1));
    REQUIRE(link.sent_locations.empty());
}

TEST_CASE("rando_bridge: reconcile_server_checked is a no-op without a save", "[mth][rando]")
{
    mth::test::FakeApLink link;
    mth::ApState state;
    connect_with(state, {ap_loc_id(5)});
    mth::RandoBridge bridge(link, state); // no attach_save_state

    REQUIRE_FALSE(bridge.reconcile_server_checked(5)); // ids stay pending in ApState until inbound-ready
    REQUIRE_FALSE(bridge.is_checked(5));
    REQUIRE(link.sent_locations.empty());
}

TEST_CASE("rando_bridge: checked_slots exposes the persisted set (nullptr without a save)", "[mth][rando]")
{
    mth::test::FakeApLink link;
    link.connected = true;
    mth::ApState state;
    connect_with(state, {ap_loc_id(5), ap_loc_id(9)});
    mth::RandoBridge bridge(link, state);
    REQUIRE(bridge.checked_slots() == nullptr); // no save attached yet

    const auto path = std::filesystem::temp_directory_path() / "mthap_bridge_checkedslots.state";
    std::filesystem::remove(path);
    mth::ApSaveState save(path);
    bridge.attach_save_state(save);
    REQUIRE(bridge.checked_slots() != nullptr);
    REQUIRE(bridge.checked_slots()->empty());

    REQUIRE(bridge.reconcile_server_checked(5)); // server-collected (Collect/coop)
    bridge.on_location_collected(9);             // live player collect
    REQUIRE(*bridge.checked_slots() == std::set<int>{5, 9});
}

TEST_CASE("rando_bridge: detach stops writing the unloaded save and clears its dedup", "[mth][rando]")
{
    const auto path_a = std::filesystem::temp_directory_path() / "mthap_test_bridge_detach_a.state";
    const auto path_b = std::filesystem::temp_directory_path() / "mthap_test_bridge_detach_b.state";
    std::filesystem::remove(path_a);
    std::filesystem::remove(path_b);

    mth::test::FakeApLink link;
    link.connected = true;
    mth::ApState state;
    connect_with(state, {ap_loc_id(5), ap_loc_id(6)});
    mth::RandoBridge bridge(link, state);

    mth::ApSaveState save_a(path_a);
    bridge.attach_save_state(save_a);
    bridge.on_location_collected(5);
    REQUIRE(save_a.is_checked(5));

    bridge.detach_save_state();
    bridge.on_location_collected(6);
    REQUIRE_FALSE(save_a.is_checked(6)); // the unloaded save must not keep receiving checks

    // A different save attaching next must not inherit the old one's checked-set.
    mth::ApSaveState save_b(path_b);
    bridge.attach_save_state(save_b);
    REQUIRE_FALSE(save_b.is_checked(5));
    bridge.on_location_collected(5);
    REQUIRE(save_b.is_checked(5));

    std::filesystem::remove(path_a);
    std::filesystem::remove(path_b);
}

TEST_CASE("rando_bridge: detach with no save attached is a no-op", "[mth][rando]")
{
    mth::test::FakeApLink link;
    mth::ApState state;
    connect_with(state, {ap_loc_id(5)});
    mth::RandoBridge bridge(link, state);

    bridge.detach_save_state();
    REQUIRE(bridge.checked_slots() == nullptr);
}
