#pragma once

namespace mth
{

// FixedUpdate runs at either 60 or 120 Hz, so a tick count buys half as much wall-clock at 120. Every
// threshold below is therefore sized in ticks at the FASTER rate and simply over-waits at 60: waiting too long
// only delays re-arming, while waiting too little at 120 Hz re-opens the echo storm (#125). Ticks, not a
// clock, because ticks stop when the game does.
inline constexpr int kMaxFixedUpdateHz = 120;
[[nodiscard]] inline constexpr int ticks_for_seconds(double s)
{
    return static_cast<int>(s * kMaxFixedUpdateHz);
}

// Decides when a local death should be broadcast for deathlink, and when an inbound death is safe to apply.
// Broadcasts once per death and re-arms only on a SETTLED respawn (stably alive for kStableAliveTicks polls),
// so a pulsing/flickering death-state signal during a world/screen transition cannot re-broadcast the same
// death. An inbound deathlink suppresses our own outbound until we settle again, which breaks the multiworld
// echo storm (#125). Pure/testable.
//
// observe(dying, alive, gameplay_advanced) is polled each tick:
//   - `dying`  the death-guard byte. It PULSES during the death sequence, so it only triggers the (latched)
//              broadcast, never a re-arm.
//   - `alive`  a stable "truly alive" signal (health > 0). alive && !dying for kStableAliveTicks consecutive
//              polls is a settled respawn: it re-arms the broadcast and lifts inbound-echo suppression. A
//              single alive poll (health blipping >0 mid-transition) does NOT re-arm.
//   - `gameplay_advanced`  false on a tick where the world's gameplay queues did not run (a paused world or a
//              paused game). Nothing the game has queued can progress on such a tick, so no timer here ages on
//              it. A death edge still counts: one can only mean the death did land.
// note_inbound_death() is called for every received inbound death: suppress our outbound until we settle. The
// death it requests takes many polls to register (the game reads alive throughout), so it also holds us
// "not settled" for kInboundDeathGraceTicks or until the death lands - otherwise those alive polls settle us
// and lift the suppression before the death they were meant to suppress ever arrives. That grace counts
// gameplay ticks because a menu holds a requested death for as long as it stays open: spent on frozen ticks it
// lapses mid-menu, and the queued death broadcasts the moment it lands.
// stably_alive() is the settled-respawn signal DeathHooks gates an inbound PlayerDie on (never mid-death or
// mid-transition), which also stops the storm from the receiving side.
class DeathBroadcastGate
{
  public:
    // Consecutive stably-alive polls that count as a settled respawn. Debounces the health/guard-byte flicker
    // seen during a world/screen transition. Costs at most this much delay before a respawn re-arms.
    static constexpr int kStableAliveTicks = ticks_for_seconds(0.25); // 30: 0.25s at 120 Hz, 0.5s at 60 Hz

    // GAMEPLAY polls to wait for a death we requested (inbound PlayerDie) to show up in the guard byte /
    // health: the game keeps reading alive for ~0.7s after PlayerDie returns, so those polls must not count as
    // settled. Comfortably over the observed latency at either rate; over-waiting only delays re-arming after a
    // requested death that never landed.
    static constexpr int kInboundDeathGraceTicks = ticks_for_seconds(2.0); // 240: 2s at 120 Hz, 4s at 60 Hz

    // Returns true iff this is a fresh local death that should be broadcast (not an echo of a received death).
    bool observe(bool dying, bool alive, bool gameplay_advanced)
    {
        if (!gameplay_advanced)
        {
            // Frozen tick: nothing queued can land and no settling time passes, so hold every timer.
        }
        else if (inbound_grace_ > 0)
        {
            // A death we requested is still landing: the game reads alive for many polls after PlayerDie
            // returns. Those polls are not a settled respawn - counting them would lift the suppression we
            // armed for this very death and echo it straight back (#125).
            --inbound_grace_;
            alive_streak_ = 0;
        }
        else if (alive && !dying)
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
            inbound_grace_ = 0; // the death we were waiting on registered; suppress-until-settled takes over
            if (latched_)
                return false; // already handled this death; survives the guard byte's mid-death flicker
            latched_ = true;
            if (suppress_)
            {
                suppressed_death_ = true; // log only, drained by take_suppressed_death()
                return false;             // a death taken from a received deathlink: do not echo it back
            }
            return true;
        }
        return false;
    }

    // A received inbound death: suppress our own outbound broadcasts until we settle (stably respawn), so the
    // death we take from it - or any death during the exchange - is not echoed back into the multiworld.
    void note_inbound_death()
    {
        suppress_ = true;
        inbound_grace_ = kInboundDeathGraceTicks; // hold "not settled" until the death lands, or the grace lapses
    }

    // Log only: true once per death edge swallowed as an inbound echo.
    bool take_suppressed_death()
    {
        const bool v = suppressed_death_;
        suppressed_death_ = false;
        return v;
    }

    // True once the player has been stably alive (alive && !dying) for kStableAliveTicks polls: a settled
    // state where applying an inbound PlayerDie is safe (not mid-death, not mid-transition).
    [[nodiscard]] bool stably_alive() const
    {
        return alive_streak_ >= kStableAliveTicks;
    }

  private:
    int alive_streak_ = 0;
    int inbound_grace_ = 0;
    bool latched_ = false;
    bool suppress_ = false;
    bool suppressed_death_ = false; // log only, drained by take_suppressed_death()
};

} // namespace mth
