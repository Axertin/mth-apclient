#include "mth/hooks/chest_hooks.hpp"

#include <cstdint>
#include <set>

#include "mth/core/game_layout.hpp"
#include "mth/core/game_symbols.hpp"
#include "mth/hooks/game_tables.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

namespace
{

mth::LockRegistry *g_locks = nullptr;

std::set<int> g_logged_chest_slots; // identity log dedup (game-thread only)

// Resolve a live locked Chest's effective s_rItemCollection slot. A chest has no cached slot; its
// identity is the SpawnPoint name-key (Chest +0xa8 -> +0x40 -> +0xd0, fallback *(sp)+0x28), scanned
// against s_rItemCollection + warp-remapped exactly as the chest ctor's own gate does. -1 if unmatched.
[[nodiscard]] int resolve_chest_slot(void *self)
{
    if (!mth::tables::collection_resolved() || self == nullptr)
        return -1;

    void *rcx = *reinterpret_cast<void **>(static_cast<char *>(self) + mth::layout::kKeyBlockEntityRefOff);
    void *rax = rcx != nullptr ? *reinterpret_cast<void **>(static_cast<char *>(rcx) + 0x40) : nullptr;
    std::uint64_t key = rax != nullptr ? *reinterpret_cast<std::uint64_t *>(static_cast<char *>(rax) + 0xd0) : 0;
    if (key == 0)
    {
        void *r = rax != nullptr ? *reinterpret_cast<void **>(rax) : nullptr;
        key = r != nullptr ? *reinterpret_cast<std::uint64_t *>(static_cast<char *>(r) + 0x28) : 0;
    }
    if (key == 0)
        return -1;

    int matched = -1;
    for (int i = 0; i < mth::layout::kCollectionScanCap; ++i)
    {
        if (mth::tables::collection_name_key(i) == key)
        {
            matched = i;
            break;
        }
    }
    if (matched < 0)
        return -1;

    const int warp = mth::tables::collection_warp_remap(matched);
    return warp < 0 ? matched : warp;
}

void (*g_orig_chest_update)(void *, void *) = nullptr;

// A registered locked chest: clear its locked flag live so it opens with no kear. The chest ctor only
// reads the unlock bit at spawn, so an already-spawned locked chest needs this; the lock seed persists
// it for re-entry. Clearing a u8 flag is idempotent and harmless on an already-open chest.
void repl_chest_update(void *self, void *ctx)
{
    if (g_orig_chest_update)
        g_orig_chest_update(self, ctx);

    if (g_locks == nullptr)
        return;

    auto &locked = *reinterpret_cast<std::uint8_t *>(static_cast<char *>(self) + mth::layout::kChestLockedFlagOff);
    if (locked == 0)
        return; // not a locked chest (or already cleared) -> nothing to do

    const int slot = resolve_chest_slot(self);
    if (g_logged_chest_slots.insert(slot).second)
        pal::logf(pal::LogLevel::Debug, "locked Chest slot=%d", slot);

    if (slot < 0 || !g_locks->is_removed(slot))
        return;

    locked = 0;
    pal::logf(pal::LogLevel::Info, "chest: cleared kear-lock on slot=%d (registered for removal)", slot);
}

} // namespace

namespace mth
{

ChestHooks::ChestHooks(LockRegistry &registry)
{
    g_locks = &registry;
    tables::resolve();
    chest_update_ =
        ScopedHook(sym::chest_update, reinterpret_cast<void *>(&repl_chest_update), reinterpret_cast<void **>(&g_orig_chest_update), "Chest::Update");
}

ChestHooks::~ChestHooks()
{
    g_logged_chest_slots.clear();
    // g_locks nulled before the ScopedHook member removes the detour; the repl null-checks it.
    g_locks = nullptr;
}

} // namespace mth
