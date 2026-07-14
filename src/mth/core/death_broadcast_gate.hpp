#pragma once

namespace mth
{

// Decides when a local death should be broadcast for deathlink. Broadcasts once per death and re-arms only
// on a real respawn, so a pulsing/flickering death-state signal cannot fire repeatedly. Pure/testable.
//
// observe(dying, alive) is polled each tick:
//   - `dying`  the death-state trigger (the Player death-guard byte). It PULSES during the death sequence,
//              so it is used only to trigger the (latched) broadcast, never to re-arm.
//   - `alive`  a stable "truly alive" signal (health > 0). A real respawn (not dying AND alive) re-arms;
//              the guard byte going 0 mid-death does not, because health stays 0 until respawn.
// arm_suppress() is called just before applying an inbound death so its own edge is not echoed back.
class DeathBroadcastGate
{
  public:
    void arm_suppress()
    {
        suppress_ = true;
    }

    // Returns true iff this is a fresh local death that should be broadcast.
    bool observe(bool dying, bool alive)
    {
        if (dying)
        {
            if (latched_)
                return false; // already broadcast this death; survives the guard byte's mid-death flicker
            latched_ = true;
            if (suppress_)
            {
                suppress_ = false; // this death is one we applied (inbound); consume, do not broadcast
                return false;
            }
            return true;
        }
        if (alive)
            latched_ = false; // a real respawn (not dying + health > 0) re-arms for the next death
        return false;
    }

  private:
    bool latched_ = false;
    bool suppress_ = false;
};

} // namespace mth
