#pragma once

#include "mth/core/lock_registry.hpp"

namespace mth
{

// Kear-locked chest unlock. A locked Chest gates on the same SaveSlot+0x200 bit a removed lock sets,
// so it shares LockHooks' registry (owned by LockHooks): a registered slot has its locked flag cleared
// live by sweep(). Reload rides the lock seed + the chest ctor's own gate.
//
// The world exposes no "Chest" entity list, so live chests come from the native ChestConstruct hook and
// are held as weak pointers until the world tears them down.
class ChestHooks
{
  public:
    explicit ChestHooks(LockRegistry &registry);
    ~ChestHooks();
    ChestHooks(const ChestHooks &) = delete;
    ChestHooks &operator=(const ChestHooks &) = delete;

    void sweep();            // game-thread, pre-World::Update window
    void on_world_destroy(); // the world's chests are gone; drop every tracked handle
};

} // namespace mth
