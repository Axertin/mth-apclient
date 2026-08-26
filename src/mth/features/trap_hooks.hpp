#pragma once

#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "mth/core/trap_state.hpp"

namespace mth
{

// Runs the modifier traps. Owns the countdowns and performs every game-facing write, keeping
// TrapState pure. The bit goes into the runtime cheat mask only, so the effect lands the same
// frame, nothing reaches the save file, and the save's achievement byte is left alone.
class TrapHooks
{
  public:
    TrapHooks();
    ~TrapHooks();

    TrapHooks(const TrapHooks &) = delete;
    TrapHooks &operator=(const TrapHooks &) = delete;

    // Game thread. Forces the modifier on and starts its countdown. A positive seconds_override
    // replaces the table's duration; the dev console uses it to test short traps.
    TrapArm arm(int modifier_index, float seconds_override = 0.0f);

    // Any thread. Hands arm() to the next tick instead of running it here, so a caller off the game
    // thread does not mutate the countdown lists while tick() is walking them. The outcome lands in
    // the log rather than coming back to the caller.
    void queue_arm(int modifier_index, float seconds_override = 0.0f);

    // Game thread, once per frame. Ages the running traps, lifts what ran out, re-asserts the rest.
    void tick();

    // Lifts every running trap. Session change and teardown both need this, and the destructor calls
    // it: dropping the state without lifting the bits would strand a modifier on.
    void clear();

    // Render thread. Reads the snapshot tick() publishes; state_ itself stays game-thread-only.
    [[nodiscard]] std::vector<std::string> status_lines() const;

  private:
    [[nodiscard]] bool gameplay_advanced();
    [[nodiscard]] float take_delta();
    void drain_pending_arm();
    void advance_frame();
    void publish_active();
    void retry_pending_off();

    TrapState state_;
    float last_room_time_ = 0.0f;
    bool timing_source_logged_ = false;
    // Indices whose lift-write failed (pal::set_runtime_modifier returned false meaning "not right
    // now"); TrapState has already dropped them, and tick() retries these on the next call.
    std::vector<int> pending_off_;
    // Modifier indices arm() has already warned about failing to arm (NotReady). A set rather than
    // one scalar, since distinct traps can stall concurrently; erased once that index's write goes
    // through, so a later stall on the same index warns again instead of staying silent forever.
    std::set<int> notready_warned_;

    // Guards the two members the render thread touches, and nothing else: every countdown list above
    // stays game-thread-only.
    mutable std::mutex mtx_;
    std::vector<std::pair<int, float>> pending_arm_; // (modifier index, seconds override) queued for the game thread
    std::vector<ActiveTrap> snapshot_;               // what was running at the end of the last tick
};

} // namespace mth
