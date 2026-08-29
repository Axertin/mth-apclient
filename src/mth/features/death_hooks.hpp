#pragma once

#include <functional>
#include <string>

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

    // Ticks a local death waits to reach the death screen before the broadcast is dropped. The death
    // sequence spends about 1.5s in its phases and then runs a screen transition, so this is generous;
    // overshooting only holds a death that was never going to land. Wall ticks, not gameplay ticks: the
    // transition runs through a menu-ish gamestate where the world's queues are not what we wait on.
    static constexpr int kDeathConfirmTicks = ticks_for_seconds(6.0); // 720: 6s at 120 Hz, 12s at 60 Hz

    // on_local_death receives the detail for the outbound cause (the predicate only; ApLink prefixes our slot
    // name), so the manner of death can vary once the death path can tell them apart.
    DeathHooks(std::function<void(const std::string &)> on_local_death, std::function<void *()> get_player);
    ~DeathHooks();
    DeathHooks(const DeathHooks &) = delete;
    DeathHooks &operator=(const DeathHooks &) = delete;

    void poll();  // game-thread, per-tick: detect a fresh local death edge and broadcast it
    void kill();  // game-thread: apply an inbound death, or latch it for retry if it cannot land yet
    void reset(); // drop a latched inbound death and a held outbound one: both belong to the session being torn down

  private:
    bool gameplay_advanced();       // did the world's gameplay queues run this tick
    bool try_apply_inbound_death(); // false only when the block is transient and the death should be retried
    void drive_pending_death(bool advanced);
    void drive_pending_broadcast(); // hold a local death until the death screen says it landed

    DeathBroadcastGate gate_;
    std::function<void(const std::string &)> on_local_death_;
    std::function<void *()> get_player_;
    int last_alive_spark_{0};        // spark sampled on the last alive tick; the death drop zeroes the live value before the edge
    float last_room_time_{0.0f};     // previous room clock; unchanged means the world did not update this tick
    bool room_clock_stalled_{false}; // last tick froze with the world unpaused: a transition, not a menu
    int pending_kill_ticks_{0};      // gameplay ticks left to land a received death that could not be applied yet
    int pending_broadcast_ticks_{0}; // ticks left for our own death to reach the death screen before we drop it
};

} // namespace mth
