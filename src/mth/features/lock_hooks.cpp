#include "mth/features/lock_hooks.hpp"

#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

#include "mod/mod_api.hpp"
#include "mth/core/data/game_layout.hpp"
#include "mth/core/data/game_symbols.hpp"
#include "mth/core/data/game_tables.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

namespace
{

mth::LockRegistry *g_locks = nullptr;
std::uintptr_t g_save_manager = 0;
bool g_seed_logged = false;

std::set<int> g_logged_lock_slots;  // identity log dedup (game-thread only)
std::set<int> g_logged_chain_slots; // identity log dedup for KeyBlockChain (game-thread only)

// Resolve a live KeyBlock's effective s_rItemCollection slot (the warp-resolved id the unlock bit
// uses). KeyBlock+0x2d0 is only cached for "PairLock" locks; otherwise it is -1 and the game
// name-scans. Mirrors the SetSaveUnlocked fallback. Returns -1 if genuinely unmatched.
// out_key (optional): receives the derived name-hash compare key (0 on the cached fast path),
// for diagnosing an unresolved (-1) lock in one in-game pass (key==0 => entity chain broke).
[[nodiscard]] int resolve_effective_slot(void *self, std::uint64_t *out_key = nullptr)
{
    if (out_key != nullptr)
        *out_key = 0;
    if (!mth::tables::collection_resolved() || self == nullptr)
        return -1;

    // Fast path: PairLock locks have the ctor-cached, already-warp-resolved slot.
    const int cached = *reinterpret_cast<int *>(static_cast<char *>(self) + mth::layout::kKeyBlockSlotOff);
    if (cached >= 0)
        return cached;

    // Derive the name-hash compare key from the entity (SetSaveUnlocked b4a81b..b4a849).
    void *rcx = *reinterpret_cast<void **>(static_cast<char *>(self) + mth::layout::kKeyBlockEntityRefOff);
    void *rax = rcx != nullptr ? *reinterpret_cast<void **>(static_cast<char *>(rcx) + 0x40) : nullptr;
    std::uint64_t key = rax != nullptr ? *reinterpret_cast<std::uint64_t *>(static_cast<char *>(rax) + 0xd0) : 0;
    if (key == 0)
    {
        void *r = rax != nullptr ? *reinterpret_cast<void **>(rax) : nullptr;
        key = r != nullptr ? *reinterpret_cast<std::uint64_t *>(static_cast<char *>(r) + 0x28) : 0;
    }
    if (out_key != nullptr)
        *out_key = key;

    // Linear scan s_rItemCollection for the match.
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

    // Apply the warp remap; if no remap (<0), the matched index itself is the slot.
    const int warp = mth::tables::collection_warp_remap(matched);
    return warp < 0 ? matched : warp;
}

// A lock already spawned solid when its slot was removed won't self-open (the unlock bit is only
// read at spawn). Remove the block live; seed_removed_locks has set the persistent bit so the chain
// "all-opened" door still fires and the lock re-spawns open on room re-entry. QueueDestroy is idempotent,
// and it only queues, so it cannot mutate the entity list being walked.
void open_key_block(void *self)
{
    if (g_locks == nullptr)
        return;

    std::uint64_t key = 0;
    const int slot = resolve_effective_slot(self, &key);
    if (g_logged_lock_slots.insert(slot).second)
        pal::logf(pal::LogLevel::Debug, "KeyBlock slot=%d (raw +0x2d0=%d key=0x%llx)", slot,
                  *reinterpret_cast<int *>(static_cast<char *>(self) + mth::layout::kKeyBlockSlotOff), static_cast<unsigned long long>(key));

    if (slot < 0 || !mod::queue_destroy_available() || !g_locks->is_removed(slot))
        return;

    void *ent = *reinterpret_cast<void **>(static_cast<char *>(self) + mth::layout::kComponentEntityOff);
    void *world = ent != nullptr ? *reinterpret_cast<void **>(static_cast<char *>(ent) + mth::layout::kEntityWorldOff) : nullptr;
    if (ent != nullptr && world != nullptr)
        mod::queue_destroy_entity(world, ent, false);
}

// Resolve a live KeyBlockChain's effective slot. A chain has no cached +0x2d0; its identity is the
// SpawnPoint it gates (KeyBlockChain+0x1c0), whose name-key drives the same s_rItemCollection scan +
// warp remap the chain ctor uses to self-gate. Returns -1 if no SpawnPoint / unmatched.
[[nodiscard]] int resolve_chain_slot(void *self)
{
    if (!mth::tables::collection_resolved() || self == nullptr)
        return -1;

    void *sp = *reinterpret_cast<void **>(static_cast<char *>(self) + mth::layout::kChainSpawnPointOff);
    if (sp == nullptr)
        return -1;

    std::uint64_t key = *reinterpret_cast<std::uint64_t *>(static_cast<char *>(sp) + mth::layout::kSpawnPointNameKeyOff);
    if (key == 0)
    {
        void *r = *reinterpret_cast<void **>(sp);
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

// An already-spawned chain reads its unlock bit only at ctor time, so drive the state machine to the
// open/kill state for a removed slot; seed_removed_locks handles re-entry.
void open_chain(void *base)
{
    if (g_locks == nullptr)
        return;

    const int slot = resolve_chain_slot(base);
    if (g_logged_chain_slots.insert(slot).second)
        pal::logf(pal::LogLevel::Debug, "KeyBlockChain slot=%d (cur state=%d)", slot,
                  *reinterpret_cast<int *>(static_cast<char *>(base) + mth::layout::kChainStateCurOff));

    if (slot < 0 || !g_locks->is_removed(slot))
        return;

    // Already opening/killed? leave the native transition alone (idempotent).
    if (*reinterpret_cast<int *>(static_cast<char *>(base) + mth::layout::kChainStateCurOff) == mth::layout::kChainOpenState)
        return;

    *reinterpret_cast<int *>(static_cast<char *>(base) + mth::layout::kChainStateReqOff) = mth::layout::kChainOpenState;
    *reinterpret_cast<unsigned char *>(static_cast<char *>(base) + mth::layout::kChainStatePendingOff) = 1;
}

// No room holds anywhere near this many of one entity type; a count above it means the list head is not
// what we think it is, so walk nothing rather than read a bogus length's worth of pointers.
constexpr std::size_t kEntityListCap = 4096;
bool g_list_cap_logged = false;

// Enumerate one of the world's per-type entity lists. The entries are the components themselves, i.e. the
// same pointer the old ::Update detour received as `this`.
void for_each_entity(void *world, const char *list, void (*fn)(void *))
{
    const std::size_t count = mod::world_entity_list(world, list, nullptr, 0);
    if (count == 0)
        return;
    if (count > kEntityListCap)
    {
        if (!g_list_cap_logged)
        {
            g_list_cap_logged = true;
            pal::logf(pal::LogLevel::Warn, "locks: \"%s\" reported %zu entities; refusing to walk it", list, count);
        }
        return;
    }

    static std::vector<void *> buf; // game-thread only; keeps its capacity between ticks
    buf.assign(count, nullptr);
    // The fill call re-counts, so take the smaller of the two: a spawn between the calls must not
    // make us read past what was actually written.
    const std::size_t got = mod::world_entity_list(world, list, buf.data(), buf.size());
    const std::size_t n = got < count ? got : count;
    for (std::size_t i = 0; i < n; ++i)
        if (buf[i] != nullptr)
            fn(buf[i]);
}

} // namespace

namespace mth
{

LockHooks::LockHooks()
{
    g_locks = &locks_;
    tables::resolve();

    g_save_manager = pal::resolve_game_symbol(sym::save_manager);
    if (g_save_manager == 0)
        pal::logf(pal::LogLevel::Warn, "LockHooks: g_saveManager not resolved; lock removal disabled");
    if (!mod::queue_destroy_available())
        pal::logf(pal::LogLevel::Warn, "LockHooks: WorldQueueDestroyEntity unavailable; live lock removal disabled");
}

LockHooks::~LockHooks()
{
    g_logged_lock_slots.clear();
    g_logged_chain_slots.clear();
    g_locks = nullptr; // both sweep handlers null-check it
    g_seed_logged = false;
    g_list_cap_logged = false;
}

LockRegistry &LockHooks::locks()
{
    return locks_;
}

// Open every already-spawned lock whose slot is registered for removal. Walks the world's own per-type
// entity lists instead of detouring each class's update, so it covers both platforms with no signature.
void LockHooks::sweep_locks()
{
    void *world = mod::player_world();
    if (world == nullptr)
        return; // no live player, so no room to open anything in
    for_each_entity(world, "KeyBlock", &open_key_block);
    for_each_entity(world, "KeyBlockChain", &open_chain);
}

// Set the native unlock bit for every removed lock so the KeyBlock ctor gate spawns it open.
// Idempotent; runs each tick in the pre-World::Update window. Replicates KeyBlock::SetSaveUnlocked's math.
void LockHooks::seed_removed_locks()
{
    if (g_save_manager == 0 || !tables::collection_resolved())
        return;
    void *saveslot = pal::active_save_slot(g_save_manager);
    if (saveslot == nullptr)
        return; // no active save (e.g. title/menus)

    const std::vector<int> slots = locks_.removed_slots();
    if (slots.empty())
        return;

    auto &field = *reinterpret_cast<std::uint64_t *>(static_cast<char *>(saveslot) + layout::kSaveBlockUnlockOff);
    for (int slot : slots)
    {
        if (slot < 0 || slot >= layout::kLocationCount)
            continue;
        // 0xff is the table's "no unlock bit" sentinel and shipping rows do use it; shifting by it
        // is UB and on x86 wraps to bit 63, silently unlocking an unrelated lock in the save.
        const std::uint8_t bit = tables::collection_bit_index(slot);
        if (!tables::collection_bit_usable(bit))
        {
            pal::logf(pal::LogLevel::Warn, "locks: slot %d has no usable unlock bit (idx=%u); not seeding", slot, static_cast<unsigned>(bit));
            continue;
        }
        field |= (std::uint64_t{1} << bit);
    }

    if (!g_seed_logged)
    {
        g_seed_logged = true;
        pal::logf(pal::LogLevel::Info, "locks: seeded %zu removed lock(s) into SaveSlot unlock bitfield", slots.size());
    }
}

} // namespace mth
