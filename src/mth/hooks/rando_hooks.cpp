#include "mth/hooks/rando_hooks.hpp"

#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

#include "mth/core/ap_ids.hpp"
#include "mth/core/game_layout.hpp"
#include "mth/core/game_symbols.hpp"
#include "mth/core/lock_registry.hpp"
#include "mth/core/rando_bridge.hpp"
#include "mth/hooks/game_tables.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_hook.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_mem.hpp"
#include "pal/pal_module.hpp"

namespace
{

mth::RandoBridge *g_bridge = nullptr;

// ycWorld::QueueDestroy: tears down a pickup entity; used for already-checked locations not covered by the native gate.
void (*g_queue_destroy)(void *, void *, bool) = nullptr;

// SetItemCollected: writes a durable bitfield bit so the native Pickup::Init reload gate suppresses respawn (bitfield kinds only).
void (*g_set_item_collected)(int, bool, void *, void *) = nullptr;

// Pickup entity field offsets (build-specific; verified by startup self-check in repl_pickup_init).
constexpr std::ptrdiff_t kPickupLocIdxOff = 0x380;
constexpr std::ptrdiff_t kPickupItemTypeOff = 0x384;
bool g_pickup_offsets_ok = true; // cleared by the self-check on mismatch
bool g_shop_offsets_ok = true;   // cleared on first out-of-range read in on_shop_buy

[[nodiscard]] int &pickup_loc_idx(void *self)
{
    return *reinterpret_cast<int *>(static_cast<char *>(self) + kPickupLocIdxOff);
}
[[nodiscard]] int &pickup_item_type(void *self)
{
    return *reinterpret_cast<int *>(static_cast<char *>(self) + kPickupItemTypeOff);
}

pal::HookId g_id_pickup_init = pal::kInvalidHookId;
void (*g_orig_pickup_init)(void *, int, int) = nullptr;

void repl_pickup_init(void *self, int item_type, int loc_idx)
{
    if (g_orig_pickup_init)
        g_orig_pickup_init(self, item_type, loc_idx);

    // Offset self-check: if the entity doesn't store loc_idx at our offset, layout shifted; disable redirect.
    if (g_pickup_offsets_ok && pickup_loc_idx(self) != loc_idx)
    {
        g_pickup_offsets_ok = false;
        pal::logf(pal::LogLevel::Error, "Pickup offset check FAILED (stored=%d arg=%d); outbound redirect disabled", pickup_loc_idx(self), loc_idx);
        return;
    }
    if (!g_pickup_offsets_ok || g_bridge == nullptr)
        return;

    // Respawn suppression (fallback): already-checked location; tear down via QueueDestroy.
    // ycEntity = self+0x10, ycWorld = ycEntity+0x50.
    if (g_queue_destroy != nullptr && g_bridge->is_checked(loc_idx))
    {
        void *ent = *reinterpret_cast<void **>(static_cast<char *>(self) + 0x10);
        void *world = ent != nullptr ? *reinterpret_cast<void **>(static_cast<char *>(ent) + 0x50) : nullptr;
        if (ent != nullptr && world != nullptr)
        {
            *reinterpret_cast<unsigned *>(static_cast<char *>(self) + 0x160) |= 1u; // killed flag
            g_queue_destroy(world, ent, false);
            pal::logf(pal::LogLevel::Info, "outbound: already-checked AP location locIdx=%d -> QueueDestroy", loc_idx);
        }
        return;
    }

    if (g_bridge->is_ap_location(loc_idx))
    {
        pickup_item_type(self) = mth::layout::kApDummyItemType; // AP dummy: no-op grant + AP visual
        pal::logf(pal::LogLevel::Debug, "Pickup::Init locIdx=%d itemType=%d -> redirected to AP dummy", loc_idx, item_type);
    }
    else
    {
        pal::logf(pal::LogLevel::Debug, "Pickup::Init locIdx=%d itemType=%d -> vanilla (not an AP location)", loc_idx, item_type);
    }
}

pal::HookId g_id_pickup_on_pickup = pal::kInvalidHookId;
void (*g_orig_pickup_on_pickup)(void *, void *) = nullptr;

void repl_pickup_on_pickup(void *self, void *listener)
{
    // `self` may be an MI subobject, not the Pickup base; recover the base for the loc_idx/
    // item_type reads. The trampoline still gets the original `self`.
    void *pickup = pal::pickup_base_from_onpickup(self);

    if (g_pickup_offsets_ok && g_bridge != nullptr)
    {
        const int loc_idx = pickup_loc_idx(pickup);
        pal::logf(pal::LogLevel::Debug, "Pickup::OnPickup locIdx=%d", loc_idx);
        if (g_bridge->is_ap_location(loc_idx))
        {
            pal::logf(pal::LogLevel::Info, "outbound: collected AP location locIdx=%d", loc_idx);
            g_bridge->on_location_collected(loc_idx); // mark_checked, persist, send if connected

            // Bitfield kinds: write durable native collected-bit for native reload suppression.
            const int kind = mth::tables::native_location_kind(loc_idx);
            if (g_set_item_collected != nullptr && mth::tables::is_durable_bit_kind(kind))
            {
                g_set_item_collected(loc_idx, true, nullptr, nullptr);
                pal::logf(pal::LogLevel::Info, "outbound: SetItemCollected(locIdx=%d kind=%d) -> native reload suppression", loc_idx, kind);
            }
        }
    }
    if (g_orig_pickup_on_pickup)
        g_orig_pickup_on_pickup(self, listener);
}

// Shared shop-purchase AP logic; the PAL owns the per-platform hook + field reads and calls this.
// Returns the itemType to store: a dummy to suppress the vanilla grant (where the platform redirects), else unchanged.
int on_shop_buy(int loc_idx, int item_type)
{
    if (!g_shop_offsets_ok || g_bridge == nullptr)
        return item_type;

    // Offset self-check: garbage reads mean the ShopMenu layout shifted; disable the check.
    if (loc_idx < -1 || loc_idx >= mth::layout::kLocationCount || item_type < 0 || item_type >= mth::layout::kItemTypeCount)
    {
        g_shop_offsets_ok = false;
        pal::logf(pal::LogLevel::Error, "shop offset check FAILED (locIdx=%d itemType=%d); shop check disabled", loc_idx, item_type);
        return item_type;
    }
    if (g_bridge->is_ap_location(loc_idx))
    {
        pal::logf(pal::LogLevel::Info, "outbound: bought AP shop item locIdx=%d", loc_idx);
        g_bridge->on_location_collected(loc_idx);

        const int kind = mth::tables::native_location_kind(loc_idx);
        if (g_set_item_collected != nullptr && mth::tables::is_durable_bit_kind(kind))
        {
            g_set_item_collected(loc_idx, true, nullptr, nullptr);
            pal::logf(pal::LogLevel::Info, "outbound: SetItemCollected(locIdx=%d kind=%d) -> native reload suppression", loc_idx, kind);
        }
        return mth::layout::kApDummyItemType; // suppress vanilla grant; real item arrives via AP inbound granter
    }
    return item_type;
}

// BossComponent field offset (build-specific; verified by the in-game log in the funnels below).
constexpr std::ptrdiff_t kBossIndexOff = 0x68;

pal::HookId g_id_boss_trigger_death = pal::kInvalidHookId;
void (*g_orig_boss_trigger_death)(void *, void *, unsigned) = nullptr;
pal::HookId g_id_boss_on_defeated = pal::kInvalidHookId;
void (*g_orig_boss_on_defeated)(void *, void *) = nullptr;

// A boss reached a live-death funnel. Bridge dedups per seed+slot, so bosses that hit both funnels
// in one death (and re-entries) send at most one check.
void boss_defeated_from(void *boss_component, const char *funnel)
{
    if (g_bridge == nullptr)
        return;
    const int boss_index = *reinterpret_cast<unsigned char *>(static_cast<char *>(boss_component) + kBossIndexOff);
    if (!mth::is_boss_index(boss_index))
    {
        pal::logf(pal::LogLevel::Warn, "boss: %s index=%d out of range; offset may have shifted", funnel, boss_index);
        return;
    }
    const int slot = mth::boss_location_slot(boss_index);
    pal::logf(pal::LogLevel::Info, "outbound: boss defeated index=%d -> loc slot=%d (%s)", boss_index, slot, funnel);
    if (g_bridge->is_ap_location(slot))
        g_bridge->on_location_collected(slot);
    else
        pal::logf(pal::LogLevel::Debug, "boss: slot=%d not a valid AP location (apworld may not define this boss)", slot);
}

void repl_boss_trigger_death(void *self, void *params, unsigned a3)
{
    if (g_orig_boss_trigger_death)
        g_orig_boss_trigger_death(self, params, a3);
    boss_defeated_from(self, "TriggerDeathSequence");
}

void repl_boss_on_defeated(void *self, void *reward_info)
{
    if (g_orig_boss_on_defeated)
        g_orig_boss_on_defeated(self, reward_info);
    boss_defeated_from(self, "OnDefeatedNoSkeleton");
}

// KeyBlock identity + pre-seed disable. Slot offset verified; pre-seed replicates SetSaveUnlocked's
// exact bit math so the game's own ctor gate opens the lock (no fragile open-state writes).
constexpr std::ptrdiff_t kKeyBlockSlotOff = 0x2d0;    // int: s_rItemCollection slot, -1 = cosmetic (PairLock only)
constexpr std::ptrdiff_t kSaveBlockUnlockOff = 0x200; // u64 lock-unlocked bitfield in the SaveSlot

mth::LockRegistry *g_locks = nullptr;
std::uintptr_t g_save_manager = 0;
bool g_seed_logged = false;

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
    const int cached = *reinterpret_cast<int *>(static_cast<char *>(self) + kKeyBlockSlotOff);
    if (cached >= 0)
        return cached;

    // Derive the name-hash compare key from the entity (SetSaveUnlocked b4a81b..b4a849).
    void *rcx = *reinterpret_cast<void **>(static_cast<char *>(self) + 0xa8);
    void *rax = rcx != nullptr ? *reinterpret_cast<void **>(static_cast<char *>(rcx) + 0x40) : nullptr;
    std::uint64_t key = rax != nullptr ? *reinterpret_cast<std::uint64_t *>(static_cast<char *>(rax) + 0xd0) : 0;
    if (key == 0)
    {
        void *r = rax != nullptr ? *reinterpret_cast<void **>(rax) : nullptr;
        key = r != nullptr ? *reinterpret_cast<std::uint64_t *>(static_cast<char *>(r) + 0x28) : 0;
    }
    if (out_key != nullptr)
        *out_key = key;

    // Linear scan s_rItemCollection (compare +0x00, stride 0x50, cap 0x168) for the match.
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

    // Apply the +0x4c warp remap; if no remap (<0), the matched index itself is the slot.
    const int warp = mth::tables::collection_warp_remap(matched);
    return warp < 0 ? matched : warp;
}

pal::HookId g_id_key_block_update = pal::kInvalidHookId;
void (*g_orig_key_block_update)(void *, void *) = nullptr;

std::set<int> g_logged_lock_slots; // identity log dedup (game-thread only)

// A lock already spawned solid when its slot was removed won't self-open (the unlock bit is only
// read at spawn). Remove the block live; seed_removed_locks has set the persistent bit so the chain
// "all-opened" door still fires and the lock re-spawns open on room re-entry. QueueDestroy is idempotent.
void repl_key_block_update(void *self, void *ctx)
{
    if (g_orig_key_block_update)
        g_orig_key_block_update(self, ctx);

    if (g_locks == nullptr)
        return;

    std::uint64_t key = 0;
    const int slot = resolve_effective_slot(self, &key);
    if (g_logged_lock_slots.insert(slot).second)
        pal::logf(pal::LogLevel::Debug, "KeyBlock slot=%d (raw +0x2d0=%d key=0x%llx)", slot,
                  *reinterpret_cast<int *>(static_cast<char *>(self) + kKeyBlockSlotOff), static_cast<unsigned long long>(key));

    if (slot < 0 || g_queue_destroy == nullptr || !g_locks->is_removed(slot))
        return;

    // ycEntity = self+0x10, ycWorld = ycEntity+0x50 (same idiom as repl_pickup_init).
    void *ent = *reinterpret_cast<void **>(static_cast<char *>(self) + 0x10);
    void *world = ent != nullptr ? *reinterpret_cast<void **>(static_cast<char *>(ent) + 0x50) : nullptr;
    if (ent != nullptr && world != nullptr)
        g_queue_destroy(world, ent, false);
}

pal::HookId hook_by_symbol(const char *symbol, void *replacement, void **trampoline, const char *label)
{
    const auto addr = pal::resolve_game_symbol(symbol);
    if (addr == 0)
    {
        pal::logf(pal::LogLevel::Warn, "RandoHooks: symbol %s (%s) not found; not hooked", symbol, label);
        return pal::kInvalidHookId;
    }
    void *target = reinterpret_cast<void *>(addr);
    const auto id = pal::hook_engine().install_hook(target, replacement, trampoline);
    if (id == pal::kInvalidHookId)
        pal::logf(pal::LogLevel::Error, "RandoHooks: failed to hook %s at %p", label, target);
    else
        pal::logf(pal::LogLevel::Info, "RandoHooks: hooked %s at %p (id=%llu)", label, target, static_cast<unsigned long long>(id));
    return id;
}

} // namespace

namespace mth
{

RandoHooks::RandoHooks(RandoBridge &bridge)
{
    g_bridge = &bridge;

    tables::resolve();
    tables::repurpose_dummy_item();

    g_queue_destroy = reinterpret_cast<void (*)(void *, void *, bool)>(pal::resolve_game_symbol(sym::queue_destroy));
    if (g_queue_destroy == nullptr)
        pal::logf(pal::LogLevel::Warn, "RandoHooks: ycWorld::QueueDestroy not resolved; checked AP pickups will respawn");

    g_set_item_collected = reinterpret_cast<void (*)(int, bool, void *, void *)>(pal::resolve_game_symbol(sym::set_item_collected));
    if (g_set_item_collected == nullptr || !tables::collection_resolved())
        pal::logf(pal::LogLevel::Warn, "RandoHooks: SetItemCollected/s_rItemCollection not resolved; bitfield-kind reload suppression disabled");

    g_id_pickup_init =
        hook_by_symbol(sym::pickup_init, reinterpret_cast<void *>(&repl_pickup_init), reinterpret_cast<void **>(&g_orig_pickup_init), "Pickup::Init");
    g_id_pickup_on_pickup = hook_by_symbol(sym::pickup_on_pickup, reinterpret_cast<void *>(&repl_pickup_on_pickup),
                                           reinterpret_cast<void **>(&g_orig_pickup_on_pickup), "Pickup::OnPickup");
    pal::install_shop_purchase_hook(&on_shop_buy);
    g_id_boss_trigger_death = hook_by_symbol(sym::boss_trigger_death_sequence, reinterpret_cast<void *>(&repl_boss_trigger_death),
                                             reinterpret_cast<void **>(&g_orig_boss_trigger_death), "BossComponent::TriggerDeathSequence");
    g_id_boss_on_defeated = hook_by_symbol(sym::boss_on_defeated_no_skeleton, reinterpret_cast<void *>(&repl_boss_on_defeated),
                                           reinterpret_cast<void **>(&g_orig_boss_on_defeated), "BossComponent::OnDefeatedNoSkeleton");

    g_locks = &locks_;
    g_save_manager = pal::resolve_game_symbol(sym::save_manager);
    if (g_save_manager == 0)
        pal::logf(pal::LogLevel::Warn, "RandoHooks: g_saveManager not resolved; lock removal disabled");
    g_id_key_block_update = hook_by_symbol(sym::key_block_update, reinterpret_cast<void *>(&repl_key_block_update),
                                           reinterpret_cast<void **>(&g_orig_key_block_update), "KeyBlock::Update");
}

RandoHooks::~RandoHooks()
{
    if (g_id_pickup_init != pal::kInvalidHookId)
        pal::hook_engine().remove_hook(g_id_pickup_init);
    if (g_id_pickup_on_pickup != pal::kInvalidHookId)
        pal::hook_engine().remove_hook(g_id_pickup_on_pickup);
    pal::remove_shop_purchase_hook();
    if (g_id_boss_trigger_death != pal::kInvalidHookId)
        pal::hook_engine().remove_hook(g_id_boss_trigger_death);
    if (g_id_boss_on_defeated != pal::kInvalidHookId)
        pal::hook_engine().remove_hook(g_id_boss_on_defeated);
    if (g_id_key_block_update != pal::kInvalidHookId)
        pal::hook_engine().remove_hook(g_id_key_block_update);
    g_logged_lock_slots.clear();
    g_locks = nullptr;
    g_seed_logged = false;
    g_bridge = nullptr;
}

LockRegistry &RandoHooks::locks()
{
    return locks_;
}

// Set the native unlock bit for every removed lock so the KeyBlock ctor gate spawns it open.
// Idempotent; runs each tick in the pre-World::Update window. Replicates KeyBlock::SetSaveUnlocked's math.
void RandoHooks::seed_removed_locks()
{
    if (g_save_manager == 0 || !tables::collection_resolved())
        return;
    void *saveslot = pal::active_save_slot(g_save_manager);
    if (saveslot == nullptr)
        return; // no active save (e.g. title/menus)

    const std::vector<int> slots = locks_.removed_slots();
    if (slots.empty())
        return;

    auto &field = *reinterpret_cast<std::uint64_t *>(static_cast<char *>(saveslot) + kSaveBlockUnlockOff);
    for (int slot : slots)
    {
        if (slot < 0 || slot >= layout::kLocationCount)
            continue;
        const std::uint8_t bit = tables::collection_bit_index(slot);
        field |= (std::uint64_t{1} << bit);
    }

    if (!g_seed_logged)
    {
        g_seed_logged = true;
        pal::logf(pal::LogLevel::Info, "locks: seeded %zu removed lock(s) into SaveSlot unlock bitfield", slots.size());
    }
}

} // namespace mth
