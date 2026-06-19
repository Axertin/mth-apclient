#pragma once

#include "mth/core/lock_registry.hpp"

namespace mth
{

// Kear-locked chest unlock. A locked chest is a Chest entity (not a KeyBlock) gated on the same
// SaveSlot+0x200 bit a removed lock sets, so it shares LockHooks' registry: a slot in the registry
// has its locked flag cleared live by a per-frame hook (reload spawns it unlocked via the existing
// lock seed + the chest ctor's own gate). The PAL owns the hook target (Chest::Update on Linux,
// Chest::UpdateState on Windows, where clang-cl folds the Update wrapper). Registry owned by LockHooks.
class ChestHooks
{
  public:
    explicit ChestHooks(LockRegistry &registry);
    ~ChestHooks();
    ChestHooks(const ChestHooks &) = delete;
    ChestHooks &operator=(const ChestHooks &) = delete;
};

} // namespace mth
