#include <string>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/save/takeover_state.hpp"

using mth::next_takeover_step;
using mth::TakeoverInputs;
using mth::TakeoverStep;

TEST_CASE("every non-terminal step fails closed when the save api is unavailable", "[takeover]")
{
    TakeoverInputs in;
    in.save_api_ready = false;
    in.gameplay_entered = true;
    REQUIRE(next_takeover_step(TakeoverStep::AwaitingMenu, in) == TakeoverStep::Failed);
    REQUIRE(next_takeover_step(TakeoverStep::Launching, in) == TakeoverStep::Failed);
}

TEST_CASE("awaiting the menu holds until the profile-menu callback advances it", "[takeover]")
{
    TakeoverInputs in;
    in.save_api_ready = true;
    in.gameplay_entered = true; // must not shortcut past the menu
    REQUIRE(next_takeover_step(TakeoverStep::AwaitingMenu, in) == TakeoverStep::AwaitingMenu);
}

TEST_CASE("launching waits for gameplay before running", "[takeover]")
{
    TakeoverInputs in;
    in.save_api_ready = true;
    in.gameplay_entered = false;
    REQUIRE(next_takeover_step(TakeoverStep::Launching, in) == TakeoverStep::Launching);

    in.gameplay_entered = true;
    REQUIRE(next_takeover_step(TakeoverStep::Launching, in) == TakeoverStep::Running);
}

TEST_CASE("a stuck wait times out into failed rather than hanging", "[takeover]")
{
    TakeoverInputs in;
    in.save_api_ready = true;
    in.frames_in_step = mth::kMaxFramesPerStep + 1;
    REQUIRE(next_takeover_step(TakeoverStep::AwaitingMenu, in) == TakeoverStep::Failed);
    REQUIRE(next_takeover_step(TakeoverStep::Launching, in) == TakeoverStep::Failed);
}

TEST_CASE("a wait at exactly the frame limit has not yet timed out", "[takeover]")
{
    TakeoverInputs in;
    in.save_api_ready = true;
    in.frames_in_step = mth::kMaxFramesPerStep;
    REQUIRE(next_takeover_step(TakeoverStep::AwaitingMenu, in) == TakeoverStep::AwaitingMenu);
    REQUIRE(next_takeover_step(TakeoverStep::Launching, in) == TakeoverStep::Launching);
}

TEST_CASE("settled steps are stable, even without the save api", "[takeover]")
{
    TakeoverInputs in;
    in.save_api_ready = false;
    for (const auto step : {TakeoverStep::Idle, TakeoverStep::Running, TakeoverStep::Failed})
    {
        REQUIRE(mth::takeover_settled(step));
        REQUIRE(next_takeover_step(step, in) == step);
    }
    REQUIRE_FALSE(mth::takeover_settled(TakeoverStep::AwaitingMenu));
    REQUIRE_FALSE(mth::takeover_settled(TakeoverStep::Launching));
}

TEST_CASE("the block latches off once the ceiling trips, rather than reopening next frame", "[takeover]")
{
    mth::InputBlockState block;
    block.rearm();
    for (int i = 0; i <= mth::kMaxInputBlockFrames; ++i)
        REQUIRE(block.advance(TakeoverStep::AwaitingMenu, false));

    // The step still wants the block; only the spent budget is holding it off, and it must stay off.
    for (int i = 0; i < 10; ++i)
        REQUIRE_FALSE(block.advance(TakeoverStep::AwaitingMenu, false));
    REQUIRE_FALSE(block.blocked());
}

TEST_CASE("a fresh claim gets a fresh frame budget", "[takeover]")
{
    mth::InputBlockState block;
    block.rearm();
    for (int i = 0; i <= mth::kMaxInputBlockFrames + 5; ++i)
        block.advance(TakeoverStep::AwaitingMenu, false);
    REQUIRE_FALSE(block.blocked());

    block.rearm();
    REQUIRE(block.advance(TakeoverStep::AwaitingMenu, false));
}

TEST_CASE("frames spent unblocked do not eat the budget", "[takeover]")
{
    mth::InputBlockState block;
    block.rearm();
    for (int i = 0; i < mth::kMaxInputBlockFrames * 2; ++i)
        REQUIRE_FALSE(block.advance(TakeoverStep::Running, false));
    REQUIRE(block.advance(TakeoverStep::AwaitingMenu, false));
}

TEST_CASE("input is blocked while the menu is being waited for and driven", "[takeover]")
{
    mth::InputBlockInputs in;
    REQUIRE(mth::block_game_input(TakeoverStep::AwaitingMenu, in));
    REQUIRE(mth::block_game_input(TakeoverStep::Launching, in));
}

TEST_CASE("input reaches the game again once the takeover is running", "[takeover]")
{
    mth::InputBlockInputs in;
    REQUIRE_FALSE(mth::block_game_input(TakeoverStep::Running, in));
    REQUIRE_FALSE(mth::block_game_input(TakeoverStep::Idle, in));
}

TEST_CASE("a failed takeover keeps swallowing input while the menu is the live gamestate", "[takeover]")
{
    mth::InputBlockInputs in;
    in.on_profile_select = true;
    REQUIRE(mth::block_game_input(TakeoverStep::Failed, in));
    in.on_profile_select = false;
    REQUIRE_FALSE(mth::block_game_input(TakeoverStep::Failed, in));
}

TEST_CASE("the ceiling releases a step that still wants the block", "[takeover]")
{
    mth::InputBlockInputs in;
    in.on_profile_select = true;
    in.frames_blocked = mth::kMaxInputBlockFrames;
    REQUIRE(mth::block_game_input(TakeoverStep::AwaitingMenu, in));
    REQUIRE(mth::block_game_input(TakeoverStep::Failed, in));

    in.frames_blocked = mth::kMaxInputBlockFrames + 1;
    REQUIRE_FALSE(mth::block_game_input(TakeoverStep::AwaitingMenu, in));
    REQUIRE_FALSE(mth::block_game_input(TakeoverStep::Launching, in));
    REQUIRE_FALSE(mth::block_game_input(TakeoverStep::Failed, in));
}

TEST_CASE("every step has a name", "[takeover]")
{
    for (const auto step : {TakeoverStep::Idle, TakeoverStep::AwaitingMenu, TakeoverStep::Launching, TakeoverStep::Running, TakeoverStep::Failed})
        REQUIRE(std::string(mth::takeover_step_name(step)) != "Unknown");
}
