#pragma once

namespace mth
{

// Edge-detects a local death (is-dying 0->1) for deathlink broadcast, and consumes a self-applied death
// (an inbound deathlink we enacted) via a one-shot suppress latch so it is not echoed back. Pure/testable:
// the caller polls observe() each tick with the live is-dying state and calls arm_suppress() when it applies
// an inbound death. Replaces the old Player::InitDeath detour (no game-symbol sig needed).
class DeathBroadcastGate
{
  public:
    // Arm the one-shot latch: the next death edge is one WE caused (inbound deathlink) -> do not broadcast it.
    void arm_suppress()
    {
        suppress_ = true;
    }

    // Returns true iff is_dying is a fresh 0->1 edge that should be broadcast (not a sustained death, and not
    // a death consumed by the suppress latch).
    bool observe(bool is_dying)
    {
        const bool fresh = is_dying && !was_dying_;
        was_dying_ = is_dying;
        if (!fresh)
            return false;
        if (suppress_)
        {
            suppress_ = false; // this death is one we applied; consume the latch, do not broadcast
            return false;
        }
        return true;
    }

  private:
    bool was_dying_ = false;
    bool suppress_ = false;
};

} // namespace mth
