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
