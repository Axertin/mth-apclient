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
    DeathHooks(std::function<void()> on_local_death, std::function<void *()> get_player);
    ~DeathHooks();
    DeathHooks(const DeathHooks &) = delete;
    DeathHooks &operator=(const DeathHooks &) = delete;

    void poll(); // game-thread, per-tick: detect a fresh local death edge and broadcast it
    void kill(); // game-thread: apply an inbound death (no-op if no Player captured / already dying / API absent)

  private:
    DeathBroadcastGate gate_;
    std::function<void()> on_local_death_;
    std::function<void *()> get_player_;
    int last_alive_spark_{0}; // spark sampled on the last alive tick; the death drop zeroes the live value before the edge
};

} // namespace mth
