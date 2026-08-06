#pragma once

#include "mth/core/lock_registry.hpp"

namespace mth
{

// twin: mth/core/lock_registry.hpp (pure removed-set).
// Kear-lock removal. sweep_locks() walks the world's own "KeyBlock" and "KeyBlockChain" entity lists:
// it removes already-spawned single-block locks whose slot is in the registry, and drives multi-block
// KeyBlockChain locks (a distinct class) to their open state. Both handlers only read then write the
// entity, so neither needs to run at a point inside the game's own update.
// seed_removed_locks() sets the persistent SaveSlot unlock bit each tick for re-entry.
class LockHooks
{
  public:
    LockHooks();
    ~LockHooks();
    LockHooks(const LockHooks &) = delete;
    LockHooks &operator=(const LockHooks &) = delete;

    LockRegistry &locks();     // populated by env/console seams
    void seed_removed_locks(); // game-thread, pre-World::Update window
    void sweep_locks();        // game-thread, same window

  private:
    LockRegistry locks_;
};

} // namespace mth
