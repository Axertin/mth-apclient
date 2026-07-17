#include <filesystem>
#include <set>

#include <catch2/catch_test_macros.hpp>

#include "mocks/fake_ap_link.hpp"
#include "mth/core/ap/ap_save_state.hpp"
#include "mth/core/ap/ap_state.hpp"
#include "mth/core/rando_bridge.hpp"
#include "pal/pal_game.hpp"

using mth::ap_loc_id;

namespace
{
// ApState is non-copyable/non-movable (mutex + atomic); populate in-place.
void connect_with(mth::ApState &s, std::vector<std::int64_t> missing)
{
    s.apply(mth::ApConnected{{}, "{}", 1, {}, std::move(missing)});
}

// Every bridge entry point is inert unless the live save is the one the AP game bound at new-game
// start. The gate is a PAL global that defaults to closed, so the connected-and-bound cases below
// open it explicitly; the dtor restores the default so a case can't leak an open gate into the next.
struct ScopedApSaveGate
{
    ScopedApSaveGate()
    {
        pal::set_ap_save_gate(true);
    }
    ~ScopedApSaveGate()
    {
        pal::set_ap_save_gate(false);
    }
};
} // namespace

TEST_CASE("rando_bridge: valid location is sent once", "[mth][rando]")
{
    ScopedApSaveGate gate;
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
    ScopedApSaveGate gate;
    mth::test::FakeApLink link;
    mth::ApState state;
    connect_with(state, {ap_loc_id(5)});
    mth::RandoBridge bridge(link, state);

    bridge.on_location_collected(7);
    REQUIRE(link.sent_locations.empty());
}

TEST_CASE("rando_bridge: negative slot is ignored", "[mth][rando]")
{
    ScopedApSaveGate gate;
    mth::test::FakeApLink link;
    mth::ApState state;
    connect_with(state, {ap_loc_id(0)});
    mth::RandoBridge bridge(link, state);

    bridge.on_location_collected(-1);
    REQUIRE(link.sent_locations.empty());
}

TEST_CASE("rando_bridge: persists checks and flushes the full set", "[mth][rando]")
{
    ScopedApSaveGate gate;
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
    ScopedApSaveGate gate;
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
    ScopedApSaveGate gate;
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
    ScopedApSaveGate gate;
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
    ScopedApSaveGate gate;
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
    ScopedApSaveGate gate;
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
    ScopedApSaveGate gate;
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
    ScopedApSaveGate gate;
    mth::test::FakeApLink link;
    mth::ApState state; // never connected
    mth::RandoBridge bridge(link, state);

    bridge.send_goal();
    REQUIRE(link.goal_calls == 0);
}

TEST_CASE("rando_bridge: reconcile_server_checked marks without sending", "[mth][rando]")
{
    ScopedApSaveGate gate;
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
    ScopedApSaveGate gate;
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
    ScopedApSaveGate gate;
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
    ScopedApSaveGate gate;
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

// Wrong-save behaviour: connecting to a server must not touch a save the AP game does not own, so
// with the gate closed the bridge acts as if the session had never connected. Everything below is
// authenticated and would fire with the gate open; the only difference is the missing gate guard.

TEST_CASE("rando_bridge: closed gate reports no AP locations and nothing checked", "[mth][rando]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_bridge_gate_query.state";
    std::filesystem::remove(path);
    mth::ApSaveState save(path);
    mth::test::FakeApLink link;
    link.connected = true;
    mth::ApState state;
    connect_with(state, {ap_loc_id(5), ap_loc_id(9)});
    mth::RandoBridge bridge(link, state);
    bridge.attach_save_state(save);

    {
        ScopedApSaveGate gate;
        bridge.on_location_collected(5);
        REQUIRE(bridge.is_ap_location(5));
        REQUIRE(bridge.is_checked(5));
        REQUIRE(bridge.checked_slots() != nullptr);
    }

    // Same bridge, same persisted check, gate now closed: every query denies the AP session exists,
    // so the pickup/shop/chest detours leave the world vanilla.
    REQUIRE_FALSE(bridge.is_ap_location(5));
    REQUIRE_FALSE(bridge.is_checked(5));
    REQUIRE(bridge.checked_slots() == nullptr);
    REQUIRE(save.is_checked(5)); // the durable record itself is untouched; only the live view is denied
}

TEST_CASE("rando_bridge: closed gate does not send or persist a collect", "[mth][rando]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_bridge_gate_collect.state";
    std::filesystem::remove(path);
    mth::ApSaveState save(path);
    mth::test::FakeApLink link;
    link.connected = true;
    mth::ApState state;
    connect_with(state, {ap_loc_id(5)});
    mth::RandoBridge bridge(link, state);
    bridge.attach_save_state(save);

    bridge.on_location_collected(5); // picked up on a save this seed never bound
    REQUIRE(link.sent_locations.empty());
    REQUIRE_FALSE(save.is_checked(5));
}

TEST_CASE("rando_bridge: closed gate does not flush the checked set", "[mth][rando]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_bridge_gate_flush.state";
    std::filesystem::remove(path);
    mth::ApSaveState save(path);
    mth::test::FakeApLink link;
    link.connected = true;
    mth::ApState state;
    connect_with(state, {ap_loc_id(5)});
    mth::RandoBridge bridge(link, state);
    bridge.attach_save_state(save);
    {
        ScopedApSaveGate gate;
        bridge.on_location_collected(5);
        link.sent_locations.clear();
    }

    bridge.flush(); // reconnected while the wrong save is loaded
    REQUIRE(link.sent_locations.empty());
}

TEST_CASE("rando_bridge: closed gate does not send the goal", "[mth][rando]")
{
    mth::test::FakeApLink link;
    link.connected = true;
    mth::ApState state;
    connect_with(state, {});
    mth::RandoBridge bridge(link, state);

    bridge.send_goal(); // final boss killed on a non-AP save
    REQUIRE(link.goal_calls == 0);

    // Not latched: the goal still sends once the AP game's own save is loaded.
    ScopedApSaveGate gate;
    bridge.send_goal();
    REQUIRE(link.goal_calls == 1);
}

TEST_CASE("rando_bridge: closed gate does not reconcile server-checked locations", "[mth][rando]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_bridge_gate_reconcile.state";
    std::filesystem::remove(path);
    mth::ApSaveState save(path);
    mth::test::FakeApLink link;
    link.connected = true;
    mth::ApState state;
    connect_with(state, {ap_loc_id(5)});
    mth::RandoBridge bridge(link, state);
    bridge.attach_save_state(save);

    REQUIRE_FALSE(bridge.reconcile_server_checked(5));
    REQUIRE_FALSE(save.is_checked(5));
}
