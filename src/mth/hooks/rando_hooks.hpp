#pragma once

#include "mth/core/lock_registry.hpp"

namespace mth
{

class RandoBridge;

// Installs Pickup::Init (spawn redirect/despawn), Pickup::OnPickup (collect detect),
// shop/boss/KeyBlock hooks. Symbols resolved at construction.
class RandoHooks
{
  public:
    RandoHooks(RandoBridge &bridge);
    ~RandoHooks();
    RandoHooks(const RandoHooks &) = delete;
    RandoHooks &operator=(const RandoHooks &) = delete;

    LockRegistry &locks();     // populated by env/console seams
    void seed_removed_locks(); // game-thread, pre-World::Update window

  private:
    LockRegistry locks_;
};

} // namespace mth
