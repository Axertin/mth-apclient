#pragma once

#include <functional>

namespace mth
{

// Hooks Player::InitDeath to DETECT a true death (HP->0) and broadcast; kill() APPLIES an inbound
// death by calling Player::TriggerDeath on the captured live Player. Edge-latched loop guard.
class DeathHooks
{
  public:
    DeathHooks(std::function<void()> on_local_death, std::function<void *()> get_player);
    ~DeathHooks();
    DeathHooks(const DeathHooks &) = delete;
    DeathHooks &operator=(const DeathHooks &) = delete;

    void kill(); // game-thread; applies an inbound death (no-op if no Player captured / already dying)
    [[nodiscard]] bool ready() const;

  private:
    bool installed_{false};
};

} // namespace mth
