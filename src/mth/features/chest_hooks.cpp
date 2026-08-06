#include "mth/features/chest_hooks.hpp"

#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

#include "mod/mod_api.hpp"
#include "mth/core/data/game_layout.hpp"
#include "mth/core/data/game_tables.hpp"
#include "pal/pal_log.hpp"

namespace
{

mth::LockRegistry *g_locks = nullptr;

std::set<int> g_logged_chest_slots; // identity log dedup (game-thread only)

// One MM_WeakPtr* per live Chest, never a raw Chest*: a chest can be freed between two sweeps and the
// weak pointer is the only thing that reports it. Game-thread only.
std::vector<void *> g_chests;

// A room holds a handful of chests, so nearing this means the registry is not being drained. Stop
// growing rather than leak a handle per construct.
constexpr std::size_t kMaxTrackedChests = 512;
bool g_overflow_logged = false;

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

// Clear the locked flag for a registered slot so the chest opens with no kear; the ctor only reads the
// unlock bit at spawn, so an already-spawned chest needs this. seed_removed_locks handles re-entry; the
// clear is idempotent.
void unlock_chest(void *base)
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

// Native ChestConstruct callback: the ctx hands back the real Chest*, so there is no per-platform
// subobject fixup and no per-frame detour.
void on_chest_construct(void *chest)
{
    void *weak = mod::weak_ptr_create(chest);
    if (weak == nullptr)
        return;
    if (g_chests.size() >= kMaxTrackedChests)
    {
        mod::weak_ptr_destroy(weak);
        if (!g_overflow_logged)
        {
            g_overflow_logged = true;
            pal::logf(pal::LogLevel::Warn, "chest: tracking %zu chests; ignoring further ones", g_chests.size());
        }
        return;
    }
    g_chests.push_back(weak);
}

void drop_all_chests()
{
    for (void *weak : g_chests)
        mod::weak_ptr_destroy(weak);
    g_chests.clear();
}

} // namespace

namespace mth
{

ChestHooks::ChestHooks(LockRegistry &registry)
{
    g_locks = &registry;
    tables::resolve();
    if (!mod::weak_ptr_api_available())
        pal::logf(pal::LogLevel::Warn, "chest: weak pointers unavailable; kear-lock clearing disabled");
    mod::install_chest_construct_hook(&on_chest_construct);
}

ChestHooks::~ChestHooks()
{
    mod::remove_chest_construct_hook(); // stop the registration callback before the registry goes away
    drop_all_chests();
    g_logged_chest_slots.clear();
    g_overflow_logged = false;
    g_locks = nullptr; // unlock_chest null-checks it
}

// Walk the live chests, clearing the lock on the registered ones and dropping whatever the game freed.
void ChestHooks::sweep()
{
    for (std::size_t i = 0; i < g_chests.size();)
    {
        void *chest = mod::weak_ptr_get(g_chests[i]);
        if (chest == nullptr)
        {
            mod::weak_ptr_destroy(g_chests[i]);
            g_chests[i] = g_chests.back(); // order does not matter; swap-erase keeps the sweep O(n)
            g_chests.pop_back();
            continue;
        }
        unlock_chest(chest);
        ++i;
    }
}

void ChestHooks::on_world_destroy()
{
    drop_all_chests();
}

} // namespace mth
