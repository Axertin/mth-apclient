#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "mocks/fake_mod_api.hpp"
#include "mod/mod_api.hpp"
#include "mth/core/data/game_layout.hpp"
#include "mth/features/death_hooks.hpp"

namespace
{
// A Player buffer big enough to hold the death-guard byte; get_player() hands out its base.
struct FakePlayer
{
    std::vector<unsigned char> bytes;
    FakePlayer() : bytes(mth::layout::kPlayerDeathGuardOff + 16, 0)
    {
    }
    void set_dying(bool d)
    {
        bytes[static_cast<std::size_t>(mth::layout::kPlayerDeathGuardOff)] = d ? 1 : 0;
    }
    void *base()
    {
        return bytes.data();
    }
};
} // namespace

// The death sequence (Player::DropDeathSpark) zeroes the live spark BEFORE the poll observes the death edge,
// so reading spark at the edge always looks sparkless. DeathHooks must snapshot spark while alive and gate on
// that. This test fails against the old read-at-the-edge implementation.
TEST_CASE("deathlink: a spark-cushioned death is not broadcast even though spark reads 0 at the edge", "[deathlink][sparkless]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);

    FakePlayer player;
    int broadcasts = 0;
    mth::DeathHooks hooks([&] { ++broadcasts; }, [&] { return player.base(); });

    // Alive with 3 sparks banked: DeathHooks snapshots the pre-death spark here.
    mth::test::recorder().health = 1.0f;
    mth::test::recorder().spark = 3;
    player.set_dying(false);
    hooks.poll();
    REQUIRE(broadcasts == 0);

    // Death: the game has already dropped every spark (live spark now 0) and health is 0.
    mth::test::recorder().health = 0.0f;
    mth::test::recorder().spark = 0;
    player.set_dying(true);
    hooks.poll();

    REQUIRE(broadcasts == 0); // cushioned death (had 3 sparks) must NOT broadcast

    mod::set_api(nullptr);
}

TEST_CASE("deathlink: a genuine sparkless death is broadcast", "[deathlink][sparkless]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);

    FakePlayer player;
    int broadcasts = 0;
    mth::DeathHooks hooks([&] { ++broadcasts; }, [&] { return player.base(); });

    // Alive but already at 0 sparks.
    mth::test::recorder().health = 1.0f;
    mth::test::recorder().spark = 0;
    player.set_dying(false);
    hooks.poll();
    REQUIRE(broadcasts == 0);

    // Death with no sparks banked.
    mth::test::recorder().health = 0.0f;
    mth::test::recorder().spark = 0;
    player.set_dying(true);
    hooks.poll();

    REQUIRE(broadcasts == 1); // sparkless death broadcasts

    mod::set_api(nullptr);
}

// Replays the in-game echo storm (#125): an inbound death is applied via PlayerDie from a settled state, but
// health/the guard byte keep reading alive for ~0.7s before the death registers. Those alive polls must not
// count as a settled respawn, or they lift the suppression armed for this very death and we broadcast it back.
TEST_CASE("deathlink: a death taken from an inbound deathlink is never echoed back", "[deathlink][echo]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);

    FakePlayer player;
    int broadcasts = 0;
    mth::DeathHooks hooks([&] { ++broadcasts; }, [&] { return player.base(); });

    // Settle: stably alive, no sparks (so any death of ours would otherwise broadcast).
    mth::test::recorder().health = 1.0f;
    mth::test::recorder().spark = 0;
    player.set_dying(false);
    for (int i = 0; i < mth::DeathBroadcastGate::kStableAliveTicks; ++i)
        hooks.poll();

    hooks.kill(); // inbound deathlink -> applied from a settled state
    REQUIRE(mth::test::recorder().deaths == 1);

    // The game still reads alive while the death sequence spins up: ~0.7s, which is twice as many polls at the
    // 120 Hz FixedUpdate rate as at 60 Hz, so drive the slower-to-register (higher tick count) case.
    for (int i = 0; i < mth::ticks_for_seconds(0.7); ++i)
        hooks.poll();

    // Now the death actually registers.
    mth::test::recorder().health = 0.0f;
    player.set_dying(true);
    hooks.poll();

    REQUIRE(broadcasts == 0); // our own inbound-caused death must not re-enter the multiworld

    mod::set_api(nullptr);
}

TEST_CASE("deathlink: respawn re-arms; a mid-death guard-byte pulse does not overwrite the held spark", "[deathlink][sparkless]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);

    FakePlayer player;
    int broadcasts = 0;
    mth::DeathHooks hooks([&] { ++broadcasts; }, [&] { return player.base(); });

    // Alive with sparks, snapshot taken.
    mth::test::recorder().health = 1.0f;
    mth::test::recorder().spark = 2;
    player.set_dying(false);
    hooks.poll();

    // Death edge: broadcast suppressed (had sparks).
    mth::test::recorder().health = 0.0f;
    mth::test::recorder().spark = 0;
    player.set_dying(true);
    hooks.poll();
    REQUIRE(broadcasts == 0);

    // Guard byte pulses to 0 mid-death while still dead (health 0, spark still 0). This must NOT be sampled as
    // a fresh alive-snapshot, and must NOT re-arm the gate.
    player.set_dying(false);
    hooks.poll();
    REQUIRE(broadcasts == 0);

    mod::set_api(nullptr);
}

namespace
{
// Stably alive with no sparks: the state an inbound death may be applied from.
void settle(mth::DeathHooks &hooks, FakePlayer &player)
{
    mth::test::recorder().health = 1.0f;
    mth::test::recorder().spark = 0;
    player.set_dying(false);
    for (int i = 0; i < mth::DeathBroadcastGate::kStableAliveTicks; ++i)
        hooks.poll();
}
} // namespace

// Two bounces landed 142ms apart in the field and both issued PlayerDie into a single death sequence: no
// advancing poll ran in between, so the gate kept reporting the state it had before the first. PlayerDie
// reaches Player::DeathEvent through the vtable, bypassing the damage pipeline, so the second call re-runs the
// death teardown on a player already in it. Suspected cause of the camera losing the player.
TEST_CASE("deathlink: a second inbound death arriving during the first is deferred", "[deathlink][echo]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);

    FakePlayer player;
    mth::DeathHooks hooks([] {}, [&] { return player.base(); });
    settle(hooks, player);

    hooks.kill(); // first bounce -> applied from a settled state
    REQUIRE(mth::test::recorder().deaths == 1);

    // Only frozen polls follow, so nothing ages the gate's timers. The player still reads alive throughout
    // (the death takes ~0.7s to register), and the freeze is a paused world rather than a stalled room clock,
    // so the in-flight death is the only thing that can defer the second bounce.
    mth::test::recorder().paused = true;
    for (int i = 0; i < 17; ++i) // 142ms of 120 Hz ticks, as measured in the field
        hooks.poll();

    hooks.kill();
    REQUIRE(mth::test::recorder().deaths == 1);

    mod::set_api(nullptr);
}

// The two freezes are not equally safe. A paused world holds a queued death and runs it when the menu closes;
// a stalled room clock with the world unpaused is a screen transition, where PlayerDie lands unpredictably.
TEST_CASE("deathlink: an inbound death during a screen transition is deferred", "[deathlink][echo]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);

    FakePlayer player;
    mth::DeathHooks hooks([] {}, [&] { return player.base(); });
    settle(hooks, player);

    // The room clock stalls while WorldIsPaused stays false: mid-transition, not a menu.
    mth::test::recorder().game_paused = true;
    hooks.poll();

    hooks.kill();
    REQUIRE(mth::test::recorder().deaths == 0);

    mod::set_api(nullptr);
}

TEST_CASE("deathlink: an inbound death during a menu is still applied", "[deathlink][echo]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);

    FakePlayer player;
    mth::DeathHooks hooks([] {}, [&] { return player.base(); });
    settle(hooks, player);

    // A paused world: the game queues the death and holds it until the menu closes, so it must keep applying.
    mth::test::recorder().paused = true;
    hooks.poll();

    hooks.kill();
    REQUIRE(mth::test::recorder().deaths == 1);

    mod::set_api(nullptr);
}

// An inbound death received while a menu is open: PlayerDie is applied from a settled state, but the game
// holds the death until the menu closes, which can be minutes. Every poll in between reads alive && !dying,
// and none of them may lift the suppression armed for that still-pending death.
TEST_CASE("deathlink: a death queued behind a menu is not echoed when the menu closes", "[deathlink][echo]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);

    FakePlayer player;
    int broadcasts = 0;
    mth::DeathHooks hooks([&] { ++broadcasts; }, [&] { return player.base(); });

    // Settle: stably alive, no sparks (so any death of ours would otherwise broadcast).
    mth::test::recorder().health = 1.0f;
    mth::test::recorder().spark = 0;
    player.set_dying(false);
    for (int i = 0; i < mth::DeathBroadcastGate::kStableAliveTicks; ++i)
        hooks.poll();

    hooks.kill(); // inbound deathlink -> PlayerDie applied, the game queues it behind the menu
    REQUIRE(mth::test::recorder().deaths == 1);

    // Menu open for a minute of ticks: the player reads alive and not dying throughout, and the gameplay
    // queues the queued death needs do not run.
    mth::test::recorder().paused = true;
    for (int i = 0; i < mth::ticks_for_seconds(60.0); ++i)
        hooks.poll();
    mth::test::recorder().paused = false;

    // Menu closes and the queued death finally registers.
    mth::test::recorder().health = 0.0f;
    player.set_dying(true);
    hooks.poll();

    REQUIRE(broadcasts == 0); // the deathlink we took must not bounce back into the multiworld

    mod::set_api(nullptr);
}

// The world pause flag only covers menus that pause the WORLD. A game-level pause skips the whole world
// update queue instead, which that flag cannot see, but it stalls the room clock.
TEST_CASE("deathlink: a death queued behind a game-level pause is not echoed either", "[deathlink][echo]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);

    FakePlayer player;
    int broadcasts = 0;
    mth::DeathHooks hooks([&] { ++broadcasts; }, [&] { return player.base(); });

    mth::test::recorder().health = 1.0f;
    mth::test::recorder().spark = 0;
    player.set_dying(false);
    for (int i = 0; i < mth::DeathBroadcastGate::kStableAliveTicks; ++i)
        hooks.poll();

    hooks.kill();
    REQUIRE(mth::test::recorder().deaths == 1);

    mth::test::recorder().game_paused = true; // world never learns it is paused; the room clock stops
    for (int i = 0; i < mth::ticks_for_seconds(60.0); ++i)
        hooks.poll();
    mth::test::recorder().game_paused = false;

    mth::test::recorder().health = 0.0f;
    player.set_dying(true);
    hooks.poll();

    REQUIRE(broadcasts == 0);

    mod::set_api(nullptr);
}

// The flip side of the two tests above: ordinary gameplay must not read as frozen. A requested death the game
// rejected outright has to stop suppressing, or the next genuine death is swallowed.
TEST_CASE("deathlink: a rejected inbound death heals and the next genuine death broadcasts", "[deathlink][echo]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);

    FakePlayer player;
    int broadcasts = 0;
    mth::DeathHooks hooks([&] { ++broadcasts; }, [&] { return player.base(); });

    mth::test::recorder().health = 1.0f;
    mth::test::recorder().spark = 0;
    player.set_dying(false);
    for (int i = 0; i < mth::DeathBroadcastGate::kStableAliveTicks; ++i)
        hooks.poll();

    hooks.kill(); // applied, but the game drops it (invulnerable): no death ever arrives
    REQUIRE(mth::test::recorder().deaths == 1);

    // Ordinary gameplay ticks, so the grace does age out.
    for (int i = 0; i < mth::DeathBroadcastGate::kInboundDeathGraceTicks + mth::DeathBroadcastGate::kStableAliveTicks; ++i)
        hooks.poll();

    // A genuine, unrelated death much later.
    mth::test::recorder().health = 0.0f;
    player.set_dying(true);
    hooks.poll();

    REQUIRE(broadcasts == 1); // not swallowed by the stale suppression

    mod::set_api(nullptr);
}

// A bounce that arrives while we are already dying is served by the death we are already taking. Dying a
// second time for one exchange, after the respawn, reads as a bug to the player.
TEST_CASE("deathlink: a bounce arriving during our own death does not kill us again", "[deathlink][retry]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);

    FakePlayer player;
    int broadcasts = 0;
    mth::DeathHooks hooks([&] { ++broadcasts; }, [&] { return player.base(); });

    // Settle, then die locally: the gate is no longer stably alive.
    mth::test::recorder().health = 1.0f;
    mth::test::recorder().spark = 0;
    player.set_dying(false);
    for (int i = 0; i < mth::DeathBroadcastGate::kStableAliveTicks; ++i)
        hooks.poll();
    mth::test::recorder().health = 0.0f;
    player.set_dying(true);
    hooks.poll();
    REQUIRE(broadcasts == 1);

    hooks.kill(); // the bounce lands while we are dying
    REQUIRE(mth::test::recorder().deaths == 0);

    // Respawn and play well past a full grace + settle: nothing may be waiting to fire.
    mth::test::recorder().health = 1.0f;
    player.set_dying(false);
    for (int i = 0; i < (mth::DeathBroadcastGate::kInboundDeathGraceTicks + mth::DeathBroadcastGate::kStableAliveTicks) * 2; ++i)
        hooks.poll();

    REQUIRE(mth::test::recorder().deaths == 0);

    mod::set_api(nullptr);
}

// The other #164 drop: a bounce during a world/screen transition, when no Player is captured. This is the
// "transitioning screens can avoid deathlinks" half of the report.
TEST_CASE("deathlink: an inbound death arriving during a transition is applied when the player returns", "[deathlink][retry]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);

    FakePlayer player;
    void *live = nullptr; // mid-transition: no player captured
    int broadcasts = 0;
    mth::DeathHooks hooks([&] { ++broadcasts; }, [&] { return live; });

    hooks.kill();
    REQUIRE(mth::test::recorder().deaths == 0);

    mth::test::recorder().health = 1.0f;
    mth::test::recorder().spark = 0;
    player.set_dying(false);
    live = player.base(); // the new room's player exists
    for (int i = 0; i < mth::DeathBroadcastGate::kInboundDeathGraceTicks + mth::DeathBroadcastGate::kStableAliveTicks + 2; ++i)
        hooks.poll();

    REQUIRE(mth::test::recorder().deaths == 1);

    mod::set_api(nullptr);
}

// The latch is bounded: a bounce that never finds a settled player is dropped, not banked indefinitely to
// kill the player at some unrelated moment much later.
TEST_CASE("deathlink: a latched inbound death expires if the player never settles", "[deathlink][retry]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);

    FakePlayer player;
    void *live = nullptr;
    int broadcasts = 0;
    mth::DeathHooks hooks([&] { ++broadcasts; }, [&] { return live; });

    hooks.kill();
    for (int i = 0; i < mth::DeathHooks::kPendingInboundDeathTicks + 1; ++i)
        hooks.poll(); // still no player: the window burns down

    mth::test::recorder().health = 1.0f;
    mth::test::recorder().spark = 0;
    player.set_dying(false);
    live = player.base();
    for (int i = 0; i < mth::DeathBroadcastGate::kInboundDeathGraceTicks + mth::DeathBroadcastGate::kStableAliveTicks + 2; ++i)
        hooks.poll();

    REQUIRE(mth::test::recorder().deaths == 0); // expired, not applied late

    mod::set_api(nullptr);
}

// One pending death, however many bounces arrive while it is pending: a multiworld storm must not chain-kill
// the player through several respawns.
TEST_CASE("deathlink: several inbound deaths arriving before the player settles apply once", "[deathlink][retry]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);

    FakePlayer player;
    void *live = nullptr;
    int broadcasts = 0;
    mth::DeathHooks hooks([&] { ++broadcasts; }, [&] { return live; });

    // Three bounces spread across the unsettled window, not stacked on one tick: a counter would queue all
    // three rather than collapsing them.
    hooks.kill();
    for (int i = 0; i < mth::DeathBroadcastGate::kStableAliveTicks; ++i)
        hooks.poll();
    hooks.kill();
    for (int i = 0; i < mth::DeathBroadcastGate::kStableAliveTicks; ++i)
        hooks.poll();
    hooks.kill();

    mth::test::recorder().health = 1.0f;
    mth::test::recorder().spark = 0;
    player.set_dying(false);
    live = player.base();
    // Long enough that a second queued death would have its own full grace + settle to land in, so this
    // fails against a counter that chain-kills through respawns.
    for (int i = 0; i < (mth::DeathBroadcastGate::kInboundDeathGraceTicks + mth::DeathBroadcastGate::kStableAliveTicks) * 3; ++i)
        hooks.poll();

    REQUIRE(mth::test::recorder().deaths == 1);

    mod::set_api(nullptr);
}

// The retry window counts GAMEPLAY ticks for the same reason every other timer here does: a menu holds the
// game still, so it must not burn the window and drop the death the player is about to take.
TEST_CASE("deathlink: a menu does not burn the pending-death retry window", "[deathlink][retry]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);

    FakePlayer player;
    void *live = nullptr;
    int broadcasts = 0;
    mth::DeathHooks hooks([&] { ++broadcasts; }, [&] { return live; });

    hooks.kill();

    mth::test::recorder().paused = true; // menu open far longer than the retry window
    for (int i = 0; i < mth::DeathHooks::kPendingInboundDeathTicks * 2; ++i)
        hooks.poll();
    mth::test::recorder().paused = false;

    mth::test::recorder().health = 1.0f;
    mth::test::recorder().spark = 0;
    player.set_dying(false);
    live = player.base();
    for (int i = 0; i < mth::DeathBroadcastGate::kInboundDeathGraceTicks + mth::DeathBroadcastGate::kStableAliveTicks + 2; ++i)
        hooks.poll();

    REQUIRE(mth::test::recorder().deaths == 1);

    mod::set_api(nullptr);
}

// The inbound grace covers a PlayerDie that is already in flight. Arming it at RECEIVE time instead pins the
// alive streak at zero for its whole length, so a latched death cannot land until the grace lapses even
// though the player settled immediately. It must land off the ordinary settle debounce.
TEST_CASE("deathlink: a latched death lands as soon as the player settles, not a grace later", "[deathlink][retry]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);

    FakePlayer player;
    void *live = nullptr;
    int broadcasts = 0;
    mth::DeathHooks hooks([&] { ++broadcasts; }, [&] { return live; });

    hooks.kill(); // no player yet: latched

    mth::test::recorder().health = 1.0f;
    mth::test::recorder().spark = 0;
    player.set_dying(false);
    live = player.base();
    for (int i = 0; i < mth::DeathBroadcastGate::kStableAliveTicks + 3; ++i)
        hooks.poll();

    REQUIRE(mth::test::recorder().deaths == 1);

    mod::set_api(nullptr);
}

// A busy multiworld: bounces keep arriving while one is latched. An arrival must not restart the settling
// clock, or the latched death never lands AND the retry window never expires - the player simply stops
// taking deathlinks for as long as the storm lasts.
TEST_CASE("deathlink: a steady stream of bounces still lands a death", "[deathlink][retry]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);

    FakePlayer player;
    void *live = nullptr;
    int broadcasts = 0;
    mth::DeathHooks hooks([&] { ++broadcasts; }, [&] { return live; });

    hooks.kill(); // first bounce arrives mid-transition

    mth::test::recorder().health = 1.0f;
    mth::test::recorder().spark = 0;
    player.set_dying(false);
    live = player.base(); // player available and alive from here on

    for (int i = 0; i < mth::ticks_for_seconds(10.0) && mth::test::recorder().deaths == 0; ++i)
    {
        if (i % mth::ticks_for_seconds(1.0) == 0)
            hooks.kill(); // another player dies every second
        hooks.poll();
    }

    REQUIRE(mth::test::recorder().deaths >= 1);

    mod::set_api(nullptr);
}

// The window runs from the FIRST deferral. If each arriving bounce re-armed it, a steady stream would push
// the deadline out indefinitely and the bound would never fire.
TEST_CASE("deathlink: a stream of bounces does not extend the retry window", "[deathlink][retry]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);

    FakePlayer player;
    void *live = nullptr;
    int broadcasts = 0;
    mth::DeathHooks hooks([&] { ++broadcasts; }, [&] { return live; });

    const int window = mth::DeathHooks::kPendingInboundDeathTicks;
    hooks.kill();
    for (int i = 0; i < window; ++i)
    {
        if (i < window / 2 && i % mth::ticks_for_seconds(0.5) == 0)
            hooks.kill(); // more bounces, but only through the first half of the window
        hooks.poll();     // no player the whole time
    }

    mth::test::recorder().health = 1.0f;
    mth::test::recorder().spark = 0;
    player.set_dying(false);
    live = player.base();
    for (int i = 0; i < mth::DeathBroadcastGate::kInboundDeathGraceTicks + mth::DeathBroadcastGate::kStableAliveTicks + 2; ++i)
        hooks.poll();

    REQUIRE(mth::test::recorder().deaths == 0); // expired on schedule despite the stream

    mod::set_api(nullptr);
}

// A bounce that applies immediately must consume the one already latched, or the latched one fires again
// after the respawn and chain-kills.
TEST_CASE("deathlink: a bounce that applies at once clears an already-latched death", "[deathlink][retry]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);

    FakePlayer player;
    int broadcasts = 0;
    void *live = nullptr;
    mth::DeathHooks hooks([&] { ++broadcasts; }, [&] { return live; });

    mth::test::recorder().health = 1.0f;
    mth::test::recorder().spark = 0;
    player.set_dying(false);
    live = player.base();
    for (int i = 0; i < mth::DeathBroadcastGate::kStableAliveTicks; ++i)
        hooks.poll();

    live = nullptr; // one tick of transition
    hooks.kill();   // bounce A: latched
    REQUIRE(mth::test::recorder().deaths == 0);

    live = player.base();
    hooks.kill(); // bounce B: still settled, so it applies right away
    REQUIRE(mth::test::recorder().deaths == 1);

    for (int i = 0; i < (mth::DeathBroadcastGate::kInboundDeathGraceTicks + mth::DeathBroadcastGate::kStableAliveTicks) * 2; ++i)
        hooks.poll();

    REQUIRE(mth::test::recorder().deaths == 1); // A did not survive to fire after the respawn

    mod::set_api(nullptr);
}

// The apply path unsettles us on the spot, so a rapid second bounce is refused and latched rather than
// dropped. The death already underway serves it, so the latch has to be cancelled: kill() checks
// death_in_progress(), but a latch outlives that check and the guard byte takes ~0.7s to appear, so the
// bounce is latched while death_in_progress() is still false.
TEST_CASE("deathlink: a death in progress cancels a latched bounce instead of killing again", "[deathlink][retry]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);

    FakePlayer player;
    mth::DeathHooks hooks([] {}, [&] { return player.base(); });
    settle(hooks, player);

    hooks.kill(); // first bounce -> applied from a settled state, which unsettles us at once
    REQUIRE(mth::test::recorder().deaths == 1);

    // A paused world: frozen ticks age nothing and we are no longer settled, so the second bounce cannot be
    // applied. It is latched for retry, and the death it belongs to has not registered yet.
    mth::test::recorder().paused = true;
    hooks.poll();
    hooks.kill();
    REQUIRE(mth::test::recorder().deaths == 1);

    // The first PlayerDie now registers: from here the latched bounce is served by the death in progress.
    mth::test::recorder().paused = false;
    mth::test::recorder().health = 0.0f;
    player.set_dying(true);
    hooks.poll();

    // Respawn and play well past a full grace + settle. The latch must have been cancelled, not merely delayed.
    mth::test::recorder().health = 1.0f;
    player.set_dying(false);
    for (int i = 0; i < (mth::DeathBroadcastGate::kInboundDeathGraceTicks + mth::DeathBroadcastGate::kStableAliveTicks) * 2; ++i)
        hooks.poll();

    REQUIRE(mth::test::recorder().deaths == 1); // one exchange, one death

    mod::set_api(nullptr);
}
