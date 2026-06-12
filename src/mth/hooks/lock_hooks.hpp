#pragma once

#include "mth/core/lock_registry.hpp"
#include "mth/hooks/scoped_hook.hpp"

namespace mth
{

// KeyBlock (kear-lock) removal: KeyBlock::Update hook removes already-spawned locks
// whose slot is in the registry; seed_removed_locks() sets the persistent SaveSlot
// unlock bit each tick so re-entries spawn open and chain doors still fire.
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
    ScopedHook key_block_update_;
};

} // namespace mth
