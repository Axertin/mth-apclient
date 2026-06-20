#pragma once

#include "mth/core/lock_registry.hpp"

namespace mth
{

// Kear-locked chest unlock. A locked Chest gates on the same SaveSlot+0x200 bit a removed lock sets,
// so it shares LockHooks' registry (owned by LockHooks): a registered slot has its locked flag cleared
// live by the PAL-owned per-frame hook. Reload rides the lock seed + the chest ctor's own gate.
class ChestHooks
{
  public:
    explicit ChestHooks(LockRegistry &registry);
    ~ChestHooks();
    ChestHooks(const ChestHooks &) = delete;
    ChestHooks &operator=(const ChestHooks &) = delete;
};

} // namespace mth
