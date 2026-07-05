#pragma once

#include "mth/core/lock_registry.hpp"
#include "mth/hooks/scoped_hook.hpp"

namespace mth
{

// twin: mth/core/lock_registry.hpp (pure removed-set).
// Kear-lock removal. The KeyBlock::Update hook removes already-spawned single-block locks whose slot
// is in the registry; the PAL-owned chain hook opens multi-block KeyBlockChain locks (a distinct
// class). seed_removed_locks() sets the persistent SaveSlot unlock bit each tick for re-entry.
class LockHooks
{
  public:
    LockHooks();
    ~LockHooks();
    LockHooks(const LockHooks &) = delete;
    LockHooks &operator=(const LockHooks &) = delete;

    LockRegistry &locks();     // populated by env/console seams
    void seed_removed_locks(); // game-thread, pre-World::Update window

  private:
    LockRegistry locks_;
    ScopedHook key_block_update_; // KeyBlock::Update is unique on both platforms; the chain hook is PAL-owned
};

} // namespace mth
