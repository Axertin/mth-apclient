#include <catch2/catch_test_macros.hpp>

#include "mth/core/death_broadcast_gate.hpp"

// observe(dying, alive, gameplay_advanced) is polled each tick. `dying` is the death-guard byte (pulses
// through a death sequence); `alive` is a stable "truly alive" signal (health > 0). A settled respawn
// (alive && !dying for kStableAliveTicks consecutive polls) re-arms the broadcast and lifts inbound-echo
// suppression. A single alive poll no longer re-arms, so the health/guard flicker seen during a world/screen
// transition cannot re-broadcast an ongoing death (#125). note_inbound_death_applied() suppresses our own outbound
// until we settle. `gameplay_advanced` is false on a tick the world spent paused: every timer here counts
// gameplay ticks, because a death the game has queued cannot land on a tick where its queues do not run.

namespace
{
// Poll a settled respawn: kStableAliveTicks consecutive stably-alive observations.
void settle(mth::DeathBroadcastGate &g)
{
    for (int i = 0; i < mth::DeathBroadcastGate::kStableAliveTicks; ++i)
        (void)g.observe(false, true, true);
}
} // namespace

TEST_CASE("death_broadcast_gate: a fresh genuine death broadcasts once", "[mth][death]")
{
    mth::DeathBroadcastGate g;
    settle(g);
    REQUIRE(g.observe(true, false, true));        // death -> fire once
    REQUIRE_FALSE(g.observe(true, false, true));  // sustained dying -> latched, no re-fire
    REQUIRE_FALSE(g.observe(false, false, true)); // guard pulses off but still dead -> no re-arm
    REQUIRE_FALSE(g.observe(true, false, true));  // guard pulses back on -> still latched
}

TEST_CASE("death_broadcast_gate: a single alive poll does NOT re-arm (transition flicker)", "[mth][death]")
{
    mth::DeathBroadcastGate g;
    settle(g);
    REQUIRE(g.observe(true, false, true));       // first death fires
    REQUIRE_FALSE(g.observe(false, true, true)); // one alive poll (health blips >0 mid-transition) -> NOT re-armed
    REQUIRE_FALSE(g.observe(true, false, true)); // ongoing death must not re-broadcast
    settle(g);                                   // a full settled respawn re-arms
    REQUIRE(g.observe(true, false, true));       // the next genuine death fires
}

TEST_CASE("death_broadcast_gate: never re-arms while dying even if health reads alive", "[mth][death]")
{
    mth::DeathBroadcastGate g;
    settle(g);
    REQUIRE(g.observe(true, false, true)); // fire
    for (int i = 0; i < mth::DeathBroadcastGate::kStableAliveTicks + 2; ++i)
        REQUIRE_FALSE(g.observe(true, true, true)); // dying AND health>0 together -> streak stays 0, no re-arm
}

TEST_CASE("death_broadcast_gate: note_inbound_death_applied suppresses our death until a settled respawn", "[mth][death]")
{
    mth::DeathBroadcastGate g;
    settle(g);
    g.note_inbound_death_applied();
    REQUIRE_FALSE(g.observe(true, false, true));  // the death we take from the inbound deathlink -> not echoed
    REQUIRE_FALSE(g.observe(false, false, true)); // still dead
    REQUIRE_FALSE(g.observe(false, true, true));  // a brief alive blip (< kStableAliveTicks) does NOT lift suppress
    REQUIRE_FALSE(g.observe(true, false, true));  // so an ongoing death stays suppressed
    settle(g);                                    // only a settled respawn lifts suppression + re-arms
    REQUIRE(g.observe(true, false, true));        // a later genuine death broadcasts again
}

TEST_CASE("death_broadcast_gate: suppression survives the delay before the requested death registers", "[mth][death]")
{
    mth::DeathBroadcastGate g;
    settle(g);
    g.note_inbound_death_applied(); // we apply PlayerDie from a settled state...
    // ...but the guard byte and health keep reading alive for many ticks before the death registers (~675ms
    // in-game). Those polls must not count as a settled respawn, or they lift the suppression we just armed.
    for (int i = 0; i < mth::DeathBroadcastGate::kInboundDeathGraceTicks - 1; ++i)
        REQUIRE_FALSE(g.observe(false, true, true));
    REQUIRE_FALSE(g.observe(true, false, true)); // the death we asked for must not echo back into the multiworld
}

TEST_CASE("death_broadcast_gate: a death queued behind a menu is still suppressed when it lands", "[mth][death]")
{
    mth::DeathBroadcastGate g;
    settle(g);
    g.note_inbound_death_applied(); // received in a menu: PlayerDie is queued, and the game holds it there
    // A menu can stay open indefinitely. Those ticks read alive && !dying but run no gameplay, so they must age
    // neither the grace waiting on the death nor the settled-respawn streak.
    for (int i = 0; i < mth::DeathBroadcastGate::kInboundDeathGraceTicks * 10; ++i)
        REQUIRE_FALSE(g.observe(false, true, false));
    REQUIRE_FALSE(g.observe(true, false, true)); // menu closes, the queued death fires -> must not echo back
}

TEST_CASE("death_broadcast_gate: a requested death that never registers stops suppressing", "[mth][death]")
{
    mth::DeathBroadcastGate g;
    settle(g);
    g.note_inbound_death_applied(); // PlayerDie no-ops (rejected while invulnerable): no death ever arrives
    for (int i = 0; i < mth::DeathBroadcastGate::kInboundDeathGraceTicks; ++i)
        (void)g.observe(false, true, true);
    settle(g);                             // the grace lapses on GAMEPLAY ticks, and a settled respawn lifts suppression
    REQUIRE(g.observe(true, false, true)); // so a later genuine death is not silently swallowed
}

TEST_CASE("death_broadcast_gate: frozen ticks never lift suppression on their own", "[mth][death]")
{
    mth::DeathBroadcastGate g;
    settle(g);
    g.note_inbound_death_applied();
    // Longer than both timers put together: the pause must produce neither a lapsed grace nor a settled respawn.
    for (int i = 0; i < (mth::DeathBroadcastGate::kInboundDeathGraceTicks + mth::DeathBroadcastGate::kStableAliveTicks) * 2; ++i)
        (void)g.observe(false, true, false);
    REQUIRE_FALSE(g.observe(true, false, true)); // the queued death, arriving on the first gameplay tick
}

TEST_CASE("death_broadcast_gate: a storm of deaths with brief alive blips never leaks a broadcast", "[mth][death]")
{
    mth::DeathBroadcastGate g;
    settle(g);
    int broadcasts = 0;
    for (int round = 0; round < 30; ++round)
    {
        g.note_inbound_death_applied(); // an inbound death keeps arriving
        if (g.observe(true, false, true))
            ++broadcasts; // we die
        if (g.observe(false, true, true))
            ++broadcasts; // health blips positive for a frame (no settle)
        if (g.observe(true, false, true))
            ++broadcasts; // and dies again
    }
    REQUIRE(broadcasts == 0); // suppression holds across the whole storm; nothing echoes back
}

TEST_CASE("death_broadcast_gate: a requested death is not settled the instant it is requested", "[mth][death]")
{
    mth::DeathBroadcastGate g;
    settle(g);
    REQUIRE(g.stably_alive());
    g.note_inbound_death_applied();
    REQUIRE_FALSE(g.stably_alive()); // a death is already in flight; another must not be applied on top
}

TEST_CASE("death_broadcast_gate: a requested death stays unsettled across a freeze", "[mth][death]")
{
    // Frozen polls age no timer, so without the zeroing in note_inbound_death_applied() the streak keeps its
    // pre-freeze value for as long as the freeze lasts. Two bounces 142ms apart both applied in the field.
    mth::DeathBroadcastGate g;
    settle(g);
    g.note_inbound_death_applied(); // first inbound death -> applied
    for (int i = 0; i < mth::DeathBroadcastGate::kStableAliveTicks * 4; ++i)
    {
        (void)g.observe(false, true, false); // the death sequence freezes gameplay
        REQUIRE_FALSE(g.stably_alive());     // a second bounce landing on any of these must be deferred
    }
}

TEST_CASE("death_broadcast_gate: stably_alive tracks the debounced respawn", "[mth][death]")
{
    mth::DeathBroadcastGate g;
    REQUIRE_FALSE(g.stably_alive()); // fresh: not yet settled
    for (int i = 0; i < mth::DeathBroadcastGate::kStableAliveTicks - 1; ++i)
        (void)g.observe(false, true, true);
    REQUIRE_FALSE(g.stably_alive()); // one short of the threshold
    (void)g.observe(false, true, true);
    REQUIRE(g.stably_alive());          // reached the threshold -> settled
    (void)g.observe(true, false, true); // a death resets it
    REQUIRE_FALSE(g.stably_alive());
    (void)g.observe(false, false, true); // being not-alive (transition) also keeps it reset
    REQUIRE_FALSE(g.stably_alive());
}
