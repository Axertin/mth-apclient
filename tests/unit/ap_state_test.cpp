#include <catch2/catch_test_macros.hpp>

#include "mth/core/ap/ap_state.hpp"
#include "mth/core/data/ability_ids.hpp"

TEST_CASE("ap_state: starts idle, unauthenticated", "[mth][ap_state]")
{
    mth::ApState s;
    REQUIRE_FALSE(s.authenticated());
    REQUIRE(s.last_item_index() == -1);
    REQUIRE_FALSE(s.is_valid_location(42));
}

TEST_CASE("ap_state: ApConnected populates slot/locations and authenticates", "[mth][ap_state]")
{
    mth::ApState s;
    s.apply(mth::ApConnected{{}, R"({"x":1})", 3, {10, 11}, {12}});
    REQUIRE(s.authenticated());
    REQUIRE(s.player_slot() == 3);
    REQUIRE(s.slot_data() == R"({"x":1})");
    REQUIRE(s.is_valid_location(10));
    REQUIRE(s.is_valid_location(12));
    REQUIRE_FALSE(s.is_valid_location(99));
    REQUIRE_FALSE(s.ossex_start());                   // defaults off
    REQUIRE(s.kear_mode() == mth::KearMode::ApItems); // defaults to the apworld's default mode
    REQUIRE_FALSE(s.deathlink());                     // defaults off
}

TEST_CASE("ap_state: ossex_start flows from ApConnected", "[mth][ap_state]")
{
    mth::ApState s;
    s.apply(mth::ApConnected{{}, "{}", 1, {}, {}, true});
    REQUIRE(s.ossex_start());
}

TEST_CASE("ap_state: deathlink flows from ApConnected", "[mth][ap_state]")
{
    mth::ApState s;
    s.apply(mth::ApConnected{{}, "{}", 1, {}, {}, false, mth::KearMode::ApItems, false, false, false, false, false, false, false, true});
    REQUIRE(s.deathlink());
}

TEST_CASE("ap_state: kear_rando flows from ApConnected", "[mth][ap_state]")
{
    mth::ApState s;
    s.apply(mth::ApConnected{{}, "{}", 1, {}, {}, false, mth::KearMode::ApItems});
    REQUIRE(s.kear_mode() == mth::KearMode::ApItems);
}

// slot_data "kear_rando" is an int mode, not a flag: 0 = vanilla (the pool carries Universal Kear items,
// id 63, which must grant real keys), 1/2 = per-lock / per-area AP items (usable keys are meaningless and
// stay pinned at zero). The client used to force suppression on for every session, so a received vanilla
// kear never raised the count (issue #130).
TEST_CASE("ap_state: vanilla kear mode does not suppress usable keys", "[mth][ap_state]")
{
    mth::ApState s;
    s.apply(mth::ApConnected{{}, "{}", 1, {}, {}, false, mth::KearMode::Vanilla});
    REQUIRE(s.kear_mode() == mth::KearMode::Vanilla);
    REQUIRE_FALSE(s.kear_keys_suppressed());
}

TEST_CASE("ap_state: AP-item kear modes suppress usable keys", "[mth][ap_state]")
{
    mth::ApState s;
    s.apply(mth::ApConnected{{}, "{}", 1, {}, {}, false, mth::KearMode::ApItems});
    REQUIRE(s.kear_keys_suppressed());

    mth::ApState area;
    area.apply(mth::ApConnected{{}, "{}", 1, {}, {}, false, mth::KearMode::AreaApItems});
    REQUIRE(area.kear_keys_suppressed());
}

TEST_CASE("ap_state: items dedup by index", "[mth][ap_state]")
{
    mth::ApState s;
    s.apply(mth::ApItemReceived{{1001, 0, 1, 0b001}});
    s.apply(mth::ApItemReceived{{1002, 1, 1, 0b010}});
    s.apply(mth::ApItemReceived{{1001, 0, 1, 0b001}}); // duplicate index 0
    REQUIRE(s.received_items().size() == 2);
    REQUIRE(s.last_item_index() == 1);
    REQUIRE(s.received_items()[1].item_id == 1002);
}

TEST_CASE("ap_state: has_received matches by item_id not index", "[mth][ap_state]")
{
    mth::ApState s;
    s.apply(mth::ApItemReceived{{3001, 0, 1, 0}});
    REQUIRE(s.has_received(3001));
    REQUIRE_FALSE(s.has_received(3000));
    REQUIRE_FALSE(s.has_received(0)); // index 0 must not match
}

TEST_CASE("ap_state: disconnect clears auth; status reflects events", "[mth][ap_state]")
{
    mth::ApState s;
    s.apply(mth::ApConnected{{}, "{}", 1, {}, {}});
    s.apply(mth::ApDisconnected{});
    REQUIRE_FALSE(s.authenticated());
    s.apply(mth::ApStatusChanged{"Connecting..."});
    REQUIRE(s.status() == "Connecting...");
    s.apply(mth::ApConnectionRefused{{"InvalidSlot", "InvalidGame"}});
    REQUIRE(s.status().find("InvalidSlot") != std::string::npos);
}

TEST_CASE("ap_state: phase starts Disconnected", "[mth][ap_state]")
{
    mth::ApState s;
    REQUIRE(s.phase() == mth::ConnectionPhase::Disconnected);
    REQUIRE(s.detail().empty());
}

TEST_CASE("ap_state: ApConnecting -> Connecting phase", "[mth][ap_state]")
{
    mth::ApState s;
    s.apply(mth::ApConnecting{});
    REQUIRE(s.phase() == mth::ConnectionPhase::Connecting);
    REQUIRE(s.detail().empty());
}

TEST_CASE("ap_state: ApConnected -> Connected phase", "[mth][ap_state]")
{
    mth::ApState s;
    s.apply(mth::ApConnected{{}, "{}", 1, {}, {}});
    REQUIRE(s.phase() == mth::ConnectionPhase::Connected);
}

TEST_CASE("ap_state: ApConnectionRefused -> Error phase with joined detail", "[mth][ap_state]")
{
    mth::ApState s;
    s.apply(mth::ApConnectionRefused{{"bad slot", "bad pw"}});
    REQUIRE(s.phase() == mth::ConnectionPhase::Error);
    REQUIRE(s.detail() == "bad slot, bad pw");
}

TEST_CASE("ap_state: ApDisconnected -> Disconnected phase", "[mth][ap_state]")
{
    mth::ApState s;
    s.apply(mth::ApConnected{{}, "{}", 1, {}, {}});
    s.apply(mth::ApDisconnected{});
    REQUIRE(s.phase() == mth::ConnectionPhase::Disconnected);
}

TEST_CASE("ap_state: ApLocationsChecked accumulates and drains once", "[mth][ap_state]")
{
    mth::ApState s;
    s.apply(mth::ApLocationsChecked{{10, 11}});
    s.apply(mth::ApLocationsChecked{{12}});
    REQUIRE(s.take_server_checked_pending() == std::vector<std::int64_t>{10, 11, 12});
    REQUIRE(s.take_server_checked_pending().empty()); // drained
}

TEST_CASE("ApState exposes lit_generator_lamp_mask from ApConnected", "[ap_state][fountain]")
{
    mth::ApState state;
    mth::ApConnected ev;
    ev.player_slot = 1;
    ev.lit_generator_lamp_mask = 0x2A;
    state.apply(mth::ApEvent{ev});
    REQUIRE(state.lit_generator_lamp_mask() == 0x2Au);
}

TEST_CASE("ap_state: reset_session drops the session stream but keeps the new connection identity", "[mth][ap_state]")
{
    mth::ApState s;
    s.apply(mth::ApConnected{{}, "{\"a\":1}", 7, {}, {mth::ap_loc_id(5)}});
    s.apply(mth::ApItemReceived{mth::ReceivedItem{42, 0, 1, 0}});
    REQUIRE(s.received_items().size() == 1);
    REQUIRE(s.last_item_index() == 0);

    s.reset_session();

    REQUIRE(s.received_items().empty());
    REQUIRE(s.last_item_index() == -1);
    REQUIRE(s.take_server_checked_pending().empty());
    // Identity survives: reset runs right after apply(ApConnected) stored the new connection.
    REQUIRE(s.player_slot() == 7);
    REQUIRE(s.slot_data() == "{\"a\":1}");
    REQUIRE(s.authenticated());
}

TEST_CASE("ap_state: an item index reused by the next server is not swallowed as a duplicate", "[mth][ap_state]")
{
    mth::ApState s;
    s.apply(mth::ApConnected{{}, "{}", 1, {}, {mth::ap_loc_id(5)}});
    s.apply(mth::ApItemReceived{mth::ReceivedItem{42, 3, 1, 0}});
    REQUIRE(s.received_items().size() == 1);

    s.reset_session();
    s.apply(mth::ApItemReceived{mth::ReceivedItem{99, 3, 1, 0}}); // same index, new server
    REQUIRE(s.received_items().size() == 1);
    REQUIRE(s.received_items()[0].item_id == 99);
}
