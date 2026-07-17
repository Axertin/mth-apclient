#include <catch2/catch_test_macros.hpp>

#include "mth/core/ap/save_slot_gate.hpp"

TEST_CASE("ap_save_gate_open: open only when authed, ready, bound, and slots match", "[mth][save_slot_gate]")
{
    REQUIRE(mth::ap_save_gate_open(true, true, 1, 1));
    REQUIRE_FALSE(mth::ap_save_gate_open(false, true, 1, 1)); // not authed
    REQUIRE_FALSE(mth::ap_save_gate_open(true, false, 1, 1)); // inbound not ready
    REQUIRE_FALSE(mth::ap_save_gate_open(true, true, -1, 1)); // unbound
    REQUIRE_FALSE(mth::ap_save_gate_open(true, true, 1, 2));  // wrong save loaded
    REQUIRE_FALSE(mth::ap_save_gate_open(true, true, 1, -1)); // no save active
}

TEST_CASE("ap_bind_on_new_game: binds live slot only when connected, ready, unbound, valid", "[mth][save_slot_gate]")
{
    REQUIRE(mth::ap_bind_on_new_game(true, true, -1, 2) == 2);   // bind
    REQUIRE(mth::ap_bind_on_new_game(true, true, 1, 2) == -1);   // already bound -> no rebind
    REQUIRE(mth::ap_bind_on_new_game(true, true, -1, -1) == -1); // slot not live yet -> wait
    REQUIRE(mth::ap_bind_on_new_game(false, true, -1, 2) == -1); // not authed
    REQUIRE(mth::ap_bind_on_new_game(true, false, -1, 2) == -1); // inbound not ready
}

TEST_CASE("ap_bind_legacy_unbound: rescues a played-but-unbound AP game, never a fresh one", "[mth][save_slot_gate]")
{
    // An AP game with progress but no recorded slot predates the gate: bind it to whatever save is
    // live so the playthrough keeps working.
    REQUIRE(mth::ap_bind_legacy_unbound(true, true, -1, 2, true, true));

    // A fresh seed is also unbound, but has no progress, so it must wait for its new-game edge
    // instead of adopting whichever save happens to be loaded. This is what keeps the gate a gate.
    REQUIRE_FALSE(mth::ap_bind_legacy_unbound(true, true, -1, 2, false, true));

    // The slot index reads 0 at the title screen, so connecting there must not bind slot 0.
    REQUIRE_FALSE(mth::ap_bind_legacy_unbound(true, true, -1, 0, true, false));

    REQUIRE_FALSE(mth::ap_bind_legacy_unbound(true, true, 1, 2, true, true));   // already bound
    REQUIRE_FALSE(mth::ap_bind_legacy_unbound(true, true, -1, -1, true, true)); // no save live yet
    REQUIRE_FALSE(mth::ap_bind_legacy_unbound(false, true, -1, 2, true, true)); // not authed
    REQUIRE_FALSE(mth::ap_bind_legacy_unbound(true, false, -1, 2, true, true)); // inbound not ready
}
