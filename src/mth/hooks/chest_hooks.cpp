#include "mth/hooks/chest_hooks.hpp"

#include <cstdint>
#include <set>

#include "mth/core/game_layout.hpp"
#include "mth/hooks/game_tables.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"

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

// PAL per-frame callback (base = the Chest entity). Clear the locked flag for a registered slot so it
// opens with no kear; the ctor only reads the unlock bit at spawn, so an already-spawned chest needs
// this. seed_removed_locks handles re-entry; the clear is idempotent.
void chest_unlock_cb(void *base)
{
    if (g_locks == nullptr)
        return;

    auto &locked = *reinterpret_cast<std::uint8_t *>(static_cast<char *>(base) + mth::layout::kChestLockedFlagOff);
    if (locked == 0)
        return; // not a locked chest (or already cleared) -> nothing to do

    const int slot = resolve_chest_slot(base);
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
    pal::install_chest_unlock_hook(&chest_unlock_cb); // PAL owns the per-platform hook (::Update / ::UpdateState + base fixup)
}

ChestHooks::~ChestHooks()
{
    pal::remove_chest_unlock_hook(); // stop the PAL-owned chest hook before the registry goes away
    g_logged_chest_slots.clear();
    g_locks = nullptr; // the callback null-checks it
}

} // namespace mth
