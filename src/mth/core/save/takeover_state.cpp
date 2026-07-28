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
