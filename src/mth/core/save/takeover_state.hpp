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

struct InputBlockInputs
{
    bool on_profile_select{false}; // the file-select menu is the live gamestate
    int frames_blocked{0};
};

// Ceiling on how long game input stays swallowed. A takeover that wedges somewhere the step timeouts
// do not cover must still hand the player back their controller.
inline constexpr int kMaxInputBlockFrames = 1800;

// Settled outcomes, as opposed to in-flight work.
[[nodiscard]] bool takeover_settled(TakeoverStep step) noexcept;

TakeoverStep next_takeover_step(TakeoverStep current, const TakeoverInputs &in);

// Whether the game should see no input this frame. The profile-select menu is live for a fraction of a
// second before the takeover drives it, which is long enough for a held confirm to open a vanilla file.
[[nodiscard]] bool block_game_input(TakeoverStep step, const InputBlockInputs &in) noexcept;

// Owns the frame budget behind that decision, which is the part with a latch in it: once the ceiling
// trips the block stays off until the next claim, rather than reopening on the following frame.
class InputBlockState
{
  public:
    // Fresh budget for a newly claimed launch.
    void rearm() noexcept;

    [[nodiscard]] bool advance(TakeoverStep step, bool on_profile_select) noexcept;

    [[nodiscard]] bool blocked() const noexcept
    {
        return blocked_;
    }

  private:
    bool blocked_{false};
    int frames_{0};
};

const char *takeover_step_name(TakeoverStep step);

} // namespace mth
