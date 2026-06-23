#include <catch2/catch_test_macros.hpp>

#include "mth/core/ap_state.hpp"

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
    REQUIRE_FALSE(s.ossex_start()); // defaults off
    REQUIRE_FALSE(s.kear_rando());  // defaults off
}

TEST_CASE("ap_state: ossex_start flows from ApConnected", "[mth][ap_state]")
{
    mth::ApState s;
    s.apply(mth::ApConnected{{}, "{}", 1, {}, {}, true});
    REQUIRE(s.ossex_start());
}

TEST_CASE("ap_state: kear_rando flows from ApConnected", "[mth][ap_state]")
{
    mth::ApState s;
    s.apply(mth::ApConnected{{}, "{}", 1, {}, {}, false, true});
    REQUIRE(s.kear_rando());
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
