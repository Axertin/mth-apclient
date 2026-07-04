#pragma once

#include <functional>

#include "mth/hooks/scoped_hook.hpp"

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
    ScopedHook init_death_;
};

} // namespace mth
