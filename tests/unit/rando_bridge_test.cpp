#include <filesystem>
#include <fstream>
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

TEST_CASE("rando_bridge: detach stops writing the released save and clears its dedup", "[mth][rando]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_test_bridge_detach.state";
    std::filesystem::remove(path);

    mth::test::FakeApLink link;
    link.connected = true;
    mth::ApState state;
    connect_with(state, {ap_loc_id(5), ap_loc_id(6)});
    mth::RandoBridge bridge(link, state);

    mth::ApSaveState save(path);
    bridge.attach_save_state(save);
    bridge.on_location_collected(5);
    REQUIRE(save.is_checked(5));

    bridge.detach_save_state();
    bridge.on_location_collected(6);
    REQUIRE_FALSE(save.is_checked(6)); // the released save must not keep receiving checks
    REQUIRE(bridge.checked_slots() == nullptr);
}

TEST_CASE("rando_bridge: a new server's flush never resends the previous save's checks (#124)", "[mth][rando]")
{
    const auto path_a = std::filesystem::temp_directory_path() / "mthap_test_bridge_server_a.state";
    const auto path_b = std::filesystem::temp_directory_path() / "mthap_test_bridge_server_b.state";
    std::filesystem::remove(path_a);
    std::filesystem::remove(path_b);

    mth::test::FakeApLink link;
    link.connected = true;
    mth::ApState state;
    connect_with(state, {ap_loc_id(165), ap_loc_id(186)});
    mth::RandoBridge bridge(link, state);

    mth::ApSaveState save_a(path_a);
    bridge.attach_save_state(save_a);
    bridge.on_location_collected(165);
    bridge.on_location_collected(186);
    link.sent_locations.clear();

    // Explicit connect to a different server: the session clear releases A's save before B's attaches.
    bridge.reset_session();
    mth::ApSaveState save_b(path_b);
    bridge.attach_save_state(save_b);
    bridge.flush();
    REQUIRE(link.sent_locations.empty()); // A's checked-set must never reach B
}

TEST_CASE("rando_bridge: reset_session re-arms the one-shot goal for the next server", "[mth][rando]")
{
    mth::test::FakeApLink link;
    link.connected = true;
    mth::ApState state;
    connect_with(state, {ap_loc_id(5)});
    mth::RandoBridge bridge(link, state);

    bridge.send_goal();
    bridge.send_goal();
    REQUIRE(link.goal_calls == 1); // one-shot within a session

    bridge.reset_session();
    bridge.send_goal();
    REQUIRE(link.goal_calls == 2); // a new session must be able to send its own goal
}

TEST_CASE("rando_bridge: a removed location reads as a checked AP location", "[mth][rando]")
{
    mth::test::FakeApLink link;
    link.connected = true;
    mth::ApState state;
    state.apply(mth::ApConnected{.slot_data = "{}", .player_slot = 1, .missing_locations = {ap_loc_id(5)}, .removed_locations = {ap_loc_id(40)}});
    mth::RandoBridge bridge(link, state);

    REQUIRE(bridge.is_ap_location(40)); // opens the suppression gates
    REQUIRE(bridge.is_checked(40));     // pickup despawn, shop sold-out, IsItemCollected redirect
    REQUIRE(bridge.is_removed(40));
    REQUIRE(bridge.is_ap_location(5));
    REQUIRE_FALSE(bridge.is_checked(5));
    REQUIRE_FALSE(bridge.is_removed(5));
    REQUIRE_FALSE(bridge.is_removed(-1));
}

TEST_CASE("rando_bridge: a removed location is never persisted, sent, or flushed", "[mth][rando]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_bridge_removed.state";
    std::filesystem::remove(path);
    mth::ApSaveState save(path);

    mth::test::FakeApLink link;
    link.connected = true;
    mth::ApState state;
    state.apply(mth::ApConnected{.slot_data = "{}", .player_slot = 1, .missing_locations = {ap_loc_id(5)}, .removed_locations = {ap_loc_id(40)}});
    mth::RandoBridge bridge(link, state);
    bridge.attach_save_state(save);

    bridge.on_location_collected(40);
    REQUIRE(link.sent_locations.empty());
    REQUIRE_FALSE(save.is_checked(40));

    bridge.on_location_collected(5); // a real check still works
    link.sent_locations.clear();
    bridge.flush();
    REQUIRE(link.sent_locations == std::vector<std::int64_t>{ap_loc_id(5)});
}

TEST_CASE("rando_bridge: a removed location is not reconciled or scouted", "[mth][rando]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_bridge_removed_reconcile.state";
    std::filesystem::remove(path);
    mth::ApSaveState save(path);

    mth::test::FakeApLink link;
    link.connected = true;
    mth::ApState state;
    state.apply(mth::ApConnected{.slot_data = "{}", .player_slot = 1, .missing_locations = {ap_loc_id(5)}, .removed_locations = {ap_loc_id(40)}});
    mth::RandoBridge bridge(link, state);
    bridge.attach_save_state(save);

    REQUIRE_FALSE(bridge.reconcile_server_checked(40)); // must not enter checked_ and get re-flushed
    REQUIRE_FALSE(save.is_checked(40));

    bridge.request_scouts({5, 40});
    REQUIRE(link.scouted_locations == std::vector<std::int64_t>{ap_loc_id(5)});

    REQUIRE(bridge.removed_slots() == std::set<std::int64_t>{ap_loc_id(40)});
}

TEST_CASE("rando_bridge: a removed location suppresses without a save attached", "[mth][rando]")
{
    mth::test::FakeApLink link;
    link.connected = true;
    mth::ApState state;
    state.apply(mth::ApConnected{.slot_data = "{}", .player_slot = 1, .missing_locations = {ap_loc_id(5)}, .removed_locations = {ap_loc_id(40)}});
    mth::RandoBridge bridge(link, state); // no attach_save_state

    REQUIRE(bridge.is_checked(40)); // session fallback must not shadow the removed set
    bridge.on_location_collected(40);
    REQUIRE(link.sent_locations.empty());
}

TEST_CASE("rando_bridge: flush excludes a removed id even from a stale statefile", "[mth][rando]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_bridge_removed_flush.state";
    std::filesystem::remove(path);
    {
        // Written directly (not through the bridge) to simulate a statefile predating the seed's prune,
        // so slot 40 is checked on disk despite slot_data now marking it removed.
        std::ofstream out(path);
        out << "c 5\n";
        out << "c 40\n";
    }
    mth::ApSaveState save(path);

    mth::test::FakeApLink link;
    link.connected = true;
    mth::ApState state;
    state.apply(mth::ApConnected{.slot_data = "{}", .player_slot = 1, .missing_locations = {ap_loc_id(5)}, .removed_locations = {ap_loc_id(40)}});
    mth::RandoBridge bridge(link, state);
    bridge.attach_save_state(save);

    bridge.flush();
    REQUIRE(link.sent_locations == std::vector<std::int64_t>{ap_loc_id(5)});
}
