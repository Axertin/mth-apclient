#pragma once

#include <cstdint>

#include "mth/core/data/game_layout.hpp"

namespace mth
{

// Name-hash reads shared by the live-object collection-slot resolves (KeyBlock, KeyBlockChain, locked
// Chest). Each mirrors the walk the game's own gate does; feed the result to
// tables::collection_slot_for_name_key.

// A spawn point's name hash: the direct u64, else the shared descriptor it points at. 0 = none.
[[nodiscard]] inline std::uint64_t object_name_key(void *obj)
{
    if (obj == nullptr)
        return 0;
    const std::uint64_t key = *reinterpret_cast<std::uint64_t *>(static_cast<char *>(obj) + layout::kSpawnPointNameKeyOff);
    if (key != 0)
        return key;
    void *descriptor = *reinterpret_cast<void **>(obj);
    return descriptor != nullptr ? *reinterpret_cast<std::uint64_t *>(static_cast<char *>(descriptor) + 0x28) : 0;
}

// The same hash reached from a component through its entity ref (+0xa8 -> +0x40 -> the object above).
// Caller null-checks `self`; every hop after it is checked here.
[[nodiscard]] inline std::uint64_t component_name_key(void *self)
{
    void *entity = *reinterpret_cast<void **>(static_cast<char *>(self) + layout::kKeyBlockEntityRefOff);
    void *obj = entity != nullptr ? *reinterpret_cast<void **>(static_cast<char *>(entity) + 0x40) : nullptr;
    return object_name_key(obj);
}

} // namespace mth
