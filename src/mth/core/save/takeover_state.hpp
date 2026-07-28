#pragma once

namespace mth
{

// Save-takeover progress. Every step is a wait on the game, not on our own work.
enum class TakeoverStep
{
    Idle,
    AwaitingMenu, // waiting for the profile-select menu to reach the state a file is confirmed from
    Launching,    // menu driven; the game is activating and starting the slot
    Running,
    Failed
};

struct TakeoverInputs
{
    bool save_api_ready{false};
    bool gameplay_entered{false};
    int frames_in_step{0};
};

// Any wait longer than this fails the takeover instead of hanging the player on a dead menu.
inline constexpr int kMaxFramesPerStep = 600;

// Settled outcomes, as opposed to in-flight work.
[[nodiscard]] bool takeover_settled(TakeoverStep step) noexcept;

TakeoverStep next_takeover_step(TakeoverStep current, const TakeoverInputs &in);

const char *takeover_step_name(TakeoverStep step);

} // namespace mth
