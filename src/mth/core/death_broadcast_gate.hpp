#pragma once

namespace mth
{

// Decides when a local death should be broadcast for deathlink, and when an inbound death is safe to apply.
// Broadcasts once per death and re-arms only on a SETTLED respawn (stably alive for kStableAliveTicks polls),
// so a pulsing/flickering death-state signal during a world/screen transition cannot re-broadcast the same
// death. An inbound deathlink suppresses our own outbound until we settle again, which breaks the multiworld
// echo storm (#125). Pure/testable.
//
// observe(dying, alive) is polled each tick:
//   - `dying`  the death-guard byte. It PULSES during the death sequence, so it only triggers the (latched)
//              broadcast, never a re-arm.
//   - `alive`  a stable "truly alive" signal (health > 0). alive && !dying for kStableAliveTicks consecutive
//              polls is a settled respawn: it re-arms the broadcast and lifts inbound-echo suppression. A
//              single alive poll (health blipping >0 mid-transition) does NOT re-arm.
// note_inbound_death() is called for every received inbound death: suppress our outbound until we settle.
// stably_alive() is the settled-respawn signal DeathHooks gates an inbound PlayerDie on (never mid-death or
// mid-transition), which also stops the storm from the receiving side.
class DeathBroadcastGate
{
  public:
    // Consecutive stably-alive polls that count as a settled respawn. Debounces the health/guard-byte flicker
    // seen during a world/screen transition. Tune against the in-game FixedUpdate rate.
    static constexpr int kStableAliveTicks = 10;

    // Returns true iff this is a fresh local death that should be broadcast (not an echo of a received death).
    bool observe(bool dying, bool alive)
    {
        if (alive && !dying)
        {
            if (alive_streak_ < kStableAliveTicks)
                ++alive_streak_;
            if (alive_streak_ >= kStableAliveTicks)
            {
                latched_ = false;  // a settled respawn re-arms the next broadcast
                suppress_ = false; // and lifts inbound-echo suppression
            }
        }
        else
        {
            alive_streak_ = 0; // dying or not-yet-alive (transition) breaks the streak
        }

        if (dying)
        {
            if (latched_)
                return false; // already handled this death; survives the guard byte's mid-death flicker
            latched_ = true;
            if (suppress_)
                return false; // a death taken from a received deathlink: do not echo it back
            return true;
        }
        return false;
    }

    // A received inbound death: suppress our own outbound broadcasts until we settle (stably respawn), so the
    // death we take from it -- or any death during the exchange -- is not echoed back into the multiworld.
    void note_inbound_death()
    {
        suppress_ = true;
    }

    // True once the player has been stably alive (alive && !dying) for kStableAliveTicks polls: a settled
    // state where applying an inbound PlayerDie is safe (not mid-death, not mid-transition).
    [[nodiscard]] bool stably_alive() const
    {
        return alive_streak_ >= kStableAliveTicks;
    }

  private:
    int alive_streak_ = 0;
    bool latched_ = false;
    bool suppress_ = false;
};

} // namespace mth
