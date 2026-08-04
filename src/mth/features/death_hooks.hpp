#pragma once

#include <functional>

#include "mth/core/death_broadcast_gate.hpp"

namespace mth
{

// Deathlink via the native modding API (no game-symbol sigs): poll() edge-detects a local death (the
// Player death-guard byte) each tick and broadcasts it; kill() applies an inbound death via
// mod::player_die(). A one-shot suppress latch stops a death we applied from echoing back. Replaces the
// old Player::InitDeath detour (DETECT) + Player::TriggerDeath call (APPLY), which broke on game rebuilds.
class DeathHooks
{
  public:
    // GAMEPLAY ticks an inbound death stays latched while it cannot be applied. Both blocks that hold it (no
    // player mid-transition, not settled after a local death) clear in 1-4s, so this is generous; past it the
    // request is stale enough that landing it would read as a random death. Gameplay ticks, not wall ticks: a
    // menu freezes the game, and a frozen tick must not burn the window (#164).
    static constexpr int kPendingInboundDeathTicks = ticks_for_seconds(10.0); // 1200: 10s at 120 Hz, 20s at 60 Hz

    DeathHooks(std::function<void()> on_local_death, std::function<void *()> get_player);
    ~DeathHooks();
    DeathHooks(const DeathHooks &) = delete;
    DeathHooks &operator=(const DeathHooks &) = delete;

    void poll();  // game-thread, per-tick: detect a fresh local death edge and broadcast it
    void kill();  // game-thread: apply an inbound death, or latch it for retry if it cannot land yet
    void reset(); // drop any latched inbound death: it belongs to the session being torn down

  private:
    bool gameplay_advanced();       // did the world's gameplay queues run this tick
    bool try_apply_inbound_death(); // false only when the block is transient and the death should be retried
    void drive_pending_death(bool gameplay_advanced);

    DeathBroadcastGate gate_;
    std::function<void()> on_local_death_;
    std::function<void *()> get_player_;
    int last_alive_spark_{0};        // spark sampled on the last alive tick; the death drop zeroes the live value before the edge
    float last_room_time_{0.0f};     // previous room clock; unchanged means the world did not update this tick
    bool room_clock_stalled_{false}; // last tick froze with the world unpaused: a transition, not a menu
    int pending_kill_ticks_{0};      // gameplay ticks left to land a received death that could not be applied yet
};

} // namespace mth
