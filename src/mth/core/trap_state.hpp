#pragma once

#include <cstddef>
#include <vector>

namespace mth
{

// What arming a trap did. The granter needs Armed and Skipped to consume the receipt, NotReady to
// retry it next tick without blocking later receipts, and Unavailable to consume it with a warning
// rather than spin forever.
enum class TrapArm
{
    Armed,       // the modifier bit is set and the countdown is running
    NotReady,    // the manager is bound but the write was refused this frame; retry
    Unavailable, // this build can never do it; give up on the receipt
    Skipped,     // the player already had that modifier on, so the trap would be a no-op
};

// One running trap: the modifier index it forces on, and the gameplay seconds left before it lifts.
struct ActiveTrap
{
    int index{0};
    float remaining{0.0f};
};

// Countdowns for the modifier traps currently forced on. Pure: it takes a delta and hands back the
// indices whose time ran out, so the owner does every game-facing write. Traps stack, one timer per
// index, in arming order.
class TrapState
{
  public:
    // Starts a trap, or extends one already running. A re-arm never shortens a live trap, because a
    // second copy of a trap the player is already suffering should not cut the first one short.
    // Ignores a non-positive duration.
    void arm(int index, float seconds);

    // Ages every trap by dt gameplay seconds and returns the indices that ran out, in arming order.
    // A non-positive dt ages nothing, which is how a paused or loading frame is spent.
    std::vector<int> advance(float dt);

    [[nodiscard]] std::vector<int> active() const;

    // Same traps as active(), in the same order, with the seconds each has left. A separate
    // accessor rather than a wider active(), since the three callers of active() want the indices
    // alone; only the console's status readout needs the remaining time.
    [[nodiscard]] std::vector<ActiveTrap> active_with_remaining() const;

    // Empties the state and returns what was running, so the owner can lift those modifiers.
    [[nodiscard]] std::vector<int> clear();

    [[nodiscard]] bool empty() const
    {
        return traps_.empty();
    }

    [[nodiscard]] std::size_t size() const
    {
        return traps_.size();
    }

  private:
    std::vector<ActiveTrap> traps_;
};

} // namespace mth
