#include "mth/core/save/takeover_state.hpp"

namespace mth
{

bool takeover_settled(TakeoverStep step) noexcept
{
    return step == TakeoverStep::Idle || step == TakeoverStep::Running || step == TakeoverStep::Failed;
}

TakeoverStep next_takeover_step(TakeoverStep current, const TakeoverInputs &in)
{
    if (takeover_settled(current))
        return current;

    // modding API inert; never drop the player into a vanilla slot at any point in the sequence
    if (!in.save_api_ready)
        return TakeoverStep::Failed;

    const bool timed_out = in.frames_in_step > kMaxFramesPerStep;

    // No default case: a new step must be handled here or the build fails.
    switch (current)
    {
    case TakeoverStep::AwaitingMenu:
        // Advanced by the profile-menu callback, which is where the live menu pointer is.
        return timed_out ? TakeoverStep::Failed : current;

    case TakeoverStep::Launching:
        if (in.gameplay_entered)
            return TakeoverStep::Running;
        return timed_out ? TakeoverStep::Failed : current;

    case TakeoverStep::Idle:
    case TakeoverStep::Running:
    case TakeoverStep::Failed:
        break; // settled, handled above
    }
    return TakeoverStep::Failed;
}

bool block_game_input(TakeoverStep step, const InputBlockInputs &in) noexcept
{
    if (in.frames_blocked > kMaxInputBlockFrames)
        return false;

    // No default case: a new step must be handled here or the build fails.
    switch (step)
    {
    case TakeoverStep::AwaitingMenu:
    case TakeoverStep::Launching:
        return true;

    // The bounce back to the title leaves the menu live for a few more frames, and a confirm landing
    // there opens exactly the vanilla file the takeover just refused to run.
    case TakeoverStep::Failed:
        return in.on_profile_select;

    case TakeoverStep::Idle:
    case TakeoverStep::Running:
        break;
    }
    return false;
}

void InputBlockState::rearm() noexcept
{
    frames_ = 0;
}

bool InputBlockState::advance(TakeoverStep step, bool on_profile_select) noexcept
{
    InputBlockInputs in;
    in.on_profile_select = on_profile_select;
    in.frames_blocked = frames_;
    blocked_ = block_game_input(step, in);
    // Only while blocking, so a tripped ceiling stops counting and cannot fall back under it.
    if (blocked_)
        ++frames_;
    return blocked_;
}

const char *takeover_step_name(TakeoverStep step)
{
    switch (step)
    {
    case TakeoverStep::Idle:
        return "Idle";
    case TakeoverStep::AwaitingMenu:
        return "AwaitingMenu";
    case TakeoverStep::Launching:
        return "Launching";
    case TakeoverStep::Running:
        return "Running";
    case TakeoverStep::Failed:
        return "Failed";
    }
    return "Unknown";
}

} // namespace mth
