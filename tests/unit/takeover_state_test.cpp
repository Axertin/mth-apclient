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

TEST_CASE("every step has a name", "[takeover]")
{
    for (const auto step : {TakeoverStep::Idle, TakeoverStep::AwaitingMenu, TakeoverStep::Launching, TakeoverStep::Running, TakeoverStep::Failed})
        REQUIRE(std::string(mth::takeover_step_name(step)) != "Unknown");
}
