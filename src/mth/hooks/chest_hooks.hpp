#pragma once

#include "mth/core/lock_registry.hpp"
#include "mth/hooks/scoped_hook.hpp"

namespace mth
{

// Kear-locked chest unlock. A locked chest is a Chest entity (not a KeyBlock) gated on the same
// SaveSlot+0x200 bit a removed lock sets, so it shares LockHooks' registry: a slot in the registry
// has its locked flag cleared live by the Chest::Update hook (reload spawns it unlocked via the
// existing lock seed + the chest ctor's own gate). The registry is owned by LockHooks; App passes it.
class ChestHooks
{
  public:
    explicit ChestHooks(LockRegistry &registry);
    ~ChestHooks();
    ChestHooks(const ChestHooks &) = delete;
    ChestHooks &operator=(const ChestHooks &) = delete;

  private:
    ScopedHook chest_update_;
};

} // namespace mth
