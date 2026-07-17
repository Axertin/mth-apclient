#include <catch2/catch_test_macros.hpp>

#include "mth/core/death_broadcast_gate.hpp"

// observe(dying, alive) is polled each tick. `dying` is the death-guard byte (pulses through a death
// sequence); `alive` is a stable "truly alive" signal (health > 0). A settled respawn (alive && !dying for
// kStableAliveTicks consecutive polls) re-arms the broadcast and lifts inbound-echo suppression -- a single
// alive poll no longer re-arms, so the health/guard flicker seen during a world/screen transition cannot
// re-broadcast an ongoing death (#125). note_inbound_death() suppresses our own outbound until we settle.

namespace
{
// Poll a settled respawn: kStableAliveTicks consecutive stably-alive observations.
void settle(mth::DeathBroadcastGate &g)
{
    for (int i = 0; i < mth::DeathBroadcastGate::kStableAliveTicks; ++i)
        (void)g.observe(false, true);
}
} // namespace

TEST_CASE("death_broadcast_gate: a fresh genuine death broadcasts once", "[mth][death]")
{
    mth::DeathBroadcastGate g;
    settle(g);
    REQUIRE(g.observe(true, false));        // death -> fire once
    REQUIRE_FALSE(g.observe(true, false));  // sustained dying -> latched, no re-fire
    REQUIRE_FALSE(g.observe(false, false)); // guard pulses off but still dead -> no re-arm
    REQUIRE_FALSE(g.observe(true, false));  // guard pulses back on -> still latched
}

TEST_CASE("death_broadcast_gate: a single alive poll does NOT re-arm (transition flicker)", "[mth][death]")
{
    mth::DeathBroadcastGate g;
    settle(g);
    REQUIRE(g.observe(true, false));       // first death fires
    REQUIRE_FALSE(g.observe(false, true)); // one alive poll (health blips >0 mid-transition) -> NOT re-armed
    REQUIRE_FALSE(g.observe(true, false)); // ongoing death must not re-broadcast
    settle(g);                             // a full settled respawn re-arms
    REQUIRE(g.observe(true, false));       // the next genuine death fires
}

TEST_CASE("death_broadcast_gate: never re-arms while dying even if health reads alive", "[mth][death]")
{
    mth::DeathBroadcastGate g;
    settle(g);
    REQUIRE(g.observe(true, false)); // fire
    for (int i = 0; i < mth::DeathBroadcastGate::kStableAliveTicks + 2; ++i)
        REQUIRE_FALSE(g.observe(true, true)); // dying AND health>0 together -> streak stays 0, no re-arm
}

TEST_CASE("death_broadcast_gate: note_inbound_death suppresses our death until a settled respawn", "[mth][death]")
{
    mth::DeathBroadcastGate g;
    settle(g);
    g.note_inbound_death();
    REQUIRE_FALSE(g.observe(true, false));  // the death we take from the inbound deathlink -> not echoed
    REQUIRE_FALSE(g.observe(false, false)); // still dead
    REQUIRE_FALSE(g.observe(false, true));  // a brief alive blip (< kStableAliveTicks) does NOT lift suppress
    REQUIRE_FALSE(g.observe(true, false));  // so an ongoing death stays suppressed
    settle(g);                              // only a settled respawn lifts suppression + re-arms
    REQUIRE(g.observe(true, false));        // a later genuine death broadcasts again
}

TEST_CASE("death_broadcast_gate: suppression survives the delay before the requested death registers", "[mth][death]")
{
    mth::DeathBroadcastGate g;
    settle(g);
    g.note_inbound_death(); // we apply PlayerDie from a settled state...
    // ...but the guard byte and health keep reading alive for many ticks before the death registers (~675ms
    // in-game). Those polls must not count as a settled respawn, or they lift the suppression we just armed.
    for (int i = 0; i < mth::DeathBroadcastGate::kInboundDeathGraceTicks - 1; ++i)
        REQUIRE_FALSE(g.observe(false, true));
    REQUIRE_FALSE(g.observe(true, false)); // the death we asked for must not echo back into the multiworld
}

TEST_CASE("death_broadcast_gate: a requested death that never registers stops suppressing", "[mth][death]")
{
    mth::DeathBroadcastGate g;
    settle(g);
    g.note_inbound_death(); // PlayerDie no-ops (or the death was deferred): no death ever arrives
    for (int i = 0; i < mth::DeathBroadcastGate::kInboundDeathGraceTicks; ++i)
        (void)g.observe(false, true);
    settle(g);                       // the grace lapses and a settled respawn lifts suppression again
    REQUIRE(g.observe(true, false)); // so a later genuine death is not silently swallowed forever
}

TEST_CASE("death_broadcast_gate: a storm of deaths with brief alive blips never leaks a broadcast", "[mth][death]")
{
    mth::DeathBroadcastGate g;
    settle(g);
    int broadcasts = 0;
    for (int round = 0; round < 30; ++round)
    {
        g.note_inbound_death(); // an inbound death keeps arriving
        if (g.observe(true, false))
            ++broadcasts; // we die
        if (g.observe(false, true))
            ++broadcasts; // health blips positive for a frame (no settle)
        if (g.observe(true, false))
            ++broadcasts; // and dies again
    }
    REQUIRE(broadcasts == 0); // suppression holds across the whole storm; nothing echoes back
}

TEST_CASE("death_broadcast_gate: stably_alive tracks the debounced respawn", "[mth][death]")
{
    mth::DeathBroadcastGate g;
    REQUIRE_FALSE(g.stably_alive()); // fresh: not yet settled
    for (int i = 0; i < mth::DeathBroadcastGate::kStableAliveTicks - 1; ++i)
        (void)g.observe(false, true);
    REQUIRE_FALSE(g.stably_alive()); // one short of the threshold
    (void)g.observe(false, true);
    REQUIRE(g.stably_alive());    // reached the threshold -> settled
    (void)g.observe(true, false); // a death resets it
    REQUIRE_FALSE(g.stably_alive());
    (void)g.observe(false, false); // being not-alive (transition) also keeps it reset
    REQUIRE_FALSE(g.stably_alive());
}
