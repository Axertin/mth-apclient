#include "mth/rando_hooks.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <set>
#include <vector>

#include "mth/core/boss_checks.hpp"
#include "mth/core/game_symbols.hpp"
#include "mth/core/lock_registry.hpp"
#include "mth/core/rando_bridge.hpp"
#include "mth/game_item_granter.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_hook.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_mem.hpp"
#include "pal/pal_module.hpp"

namespace
{

mth::RandoBridge *g_bridge = nullptr;
pal::HookId g_id = pal::kInvalidHookId;

// Items::OnPickupDone(int slot, int itemType, Player*, ycVec3 const&, int, int, unsigned int, bool)
void (*g_orig_on_pickup_done)(int, int, void *, void *, int, int, unsigned int, bool) = nullptr;

// Live Player* and position for inbound replays. All game-thread-only.
struct YcVec3
{
    float x, y, z;
};

void *g_player = nullptr;
void *g_trackable = nullptr;

// Position cached in PlayerTrackable::Update (in-context); drain() consumes it rather than
// reading in the pre-World::Update spawn window. Game-thread-only.
YcVec3 g_last_pos{};
bool g_have_pos = false;

// s_rItems: 195 entries x 0x68 bytes; storage-kind int at +0x28. Used to log grant kind.
std::uintptr_t g_s_r_items = 0;
constexpr int kItemEntryStride = 0x68;
constexpr int kStorageKindOffset = 0x28;
constexpr int kItemTypeCount = 195;

// ycWorld::QueueDestroy: tears down a pickup entity; used for already-checked locations not covered by the native gate.
void (*g_queue_destroy)(void *, void *, bool) = nullptr;

// SetItemCollected: writes a durable bitfield bit so the native Pickup::Init reload gate suppresses respawn (bitfield kinds only).
void (*g_set_item_collected)(int, bool, void *, void *) = nullptr;
std::uintptr_t g_s_r_item_collection = 0;
constexpr int kCollectionEntryStride = 0x50;
constexpr int kCollectionItemTypeOffset = 0x18;
constexpr int kLocationCount = 361;

// Pickup entity field offsets (build-specific; verified by startup self-check in repl_pickup_init).
constexpr std::ptrdiff_t kPickupLocIdxOff = 0x380;
constexpr std::ptrdiff_t kPickupItemTypeOff = 0x384;
bool g_pickup_offsets_ok = true; // cleared by the self-check on mismatch
bool g_shop_offsets_ok = true;   // cleared on first out-of-range read in repl_shop_item_present

[[nodiscard]] int &pickup_loc_idx(void *self)
{
    return *reinterpret_cast<int *>(static_cast<char *>(self) + kPickupLocIdxOff);
}
[[nodiscard]] int &pickup_item_type(void *self)
{
    return *reinterpret_cast<int *>(static_cast<char *>(self) + kPickupItemTypeOff);
}

// AP dummy: itemType 1 (Shop_Exit, dead data). Patched at startup to kind 0 (no-op grant) with
// borrowed sprite assets; AP pickups' effective itemType is redirected here.
constexpr int kApDummyItemType = 1;              // repurposed dead row
constexpr int kDummyAssetDonor = 40;             // kItemType_Treasure_Smallest (sprite asset donor)
constexpr std::ptrdiff_t kItemKindOff = 0x28;    // storage-kind (int)
constexpr std::ptrdiff_t kItemAtlasOff = 0x30;   // icon atlas (char*)
constexpr std::ptrdiff_t kItemAnimOff = 0x38;    // anim name (char*)
constexpr std::ptrdiff_t kItemPaletteOff = 0x58; // palette (char*)

// Inbound-grant queue: grant() enqueues, drain() replays inside the engine's update window.
std::mutex g_pending_mtx;
std::vector<int> g_pending;

[[nodiscard]] int storage_kind(int item_type)
{
    if (item_type < 0 || item_type >= kItemTypeCount || g_s_r_items == 0)
        return -1; // out of range or unclassifiable
    return *reinterpret_cast<const int *>(g_s_r_items + static_cast<std::uintptr_t>(item_type) * kItemEntryStride + kStorageKindOffset);
}

// Storage-kind of a location's vanilla contents (native gate keys on vanilla, not the dummy).
[[nodiscard]] int native_location_kind(int loc_idx)
{
    if (loc_idx < 0 || loc_idx >= kLocationCount || g_s_r_item_collection == 0)
        return -1;
    const int item_type =
        *reinterpret_cast<const int *>(g_s_r_item_collection + static_cast<std::uintptr_t>(loc_idx) * kCollectionEntryStride + kCollectionItemTypeOffset);
    return storage_kind(item_type);
}

// Bitfield-only kinds: SetItemCollected is side-effect-free for these (8=key, 12=bonestone, 19=fish).
// Kinds 1/9/11 write a global "have item" bit and are excluded; QueueDestroy handles them instead.
[[nodiscard]] bool is_durable_bit_kind(int kind)
{
    return kind == 8 || kind == 12 || kind == 19;
}

// Patch s_rItems[kApDummyItemType]: kind 0 (no-op grant) + sprite assets from the donor row.
// mprotect RW is defensive; s_rItems is plain .data. Best-effort: skipped if s_rItems unresolved.
void repurpose_dummy_item()
{
    if (g_s_r_items == 0)
    {
        pal::logf(pal::LogLevel::Warn, "dummy: s_rItems unresolved, AP pickups keep vanilla visual");
        return;
    }
    const std::uintptr_t dst = g_s_r_items + static_cast<std::uintptr_t>(kApDummyItemType) * kItemEntryStride;
    const std::uintptr_t src = g_s_r_items + static_cast<std::uintptr_t>(kDummyAssetDonor) * kItemEntryStride;

    if (!pal::make_writable(reinterpret_cast<void *>(dst), static_cast<std::size_t>(kItemEntryStride)))
    {
        pal::logf(pal::LogLevel::Error, "dummy: make_writable failed; s_rItems[%d] NOT patched", kApDummyItemType);
        return;
    }

    *reinterpret_cast<int *>(dst + kItemKindOff) = 0; // storage-kind None -> no grant
    *reinterpret_cast<const char **>(dst + kItemAtlasOff) = *reinterpret_cast<const char **>(src + kItemAtlasOff);
    *reinterpret_cast<const char **>(dst + kItemAnimOff) = *reinterpret_cast<const char **>(src + kItemAnimOff);
    *reinterpret_cast<const char **>(dst + kItemPaletteOff) = *reinterpret_cast<const char **>(src + kItemPaletteOff);

    pal::logf(pal::LogLevel::Info, "dummy: s_rItems[%d] -> kind 0, assets from [%d] (atlas=%s anim=%s)", kApDummyItemType, kDummyAssetDonor,
              *reinterpret_cast<const char **>(dst + kItemAtlasOff), *reinterpret_cast<const char **>(dst + kItemAnimOff));
}

pal::HookId g_id_player_ctor = pal::kInvalidHookId;
pal::HookId g_id_trackable_update = pal::kInvalidHookId;
void (*g_orig_player_ctor)(void *, void *, void *, void *) = nullptr;
void (*g_orig_trackable_update)(void *, void *) = nullptr;

void repl_player_ctor(void *self, void *entity, void *desc, void *setup)
{
    if (g_orig_player_ctor)
        g_orig_player_ctor(self, entity, desc, setup);
    g_player = self; // available before any pickup
}

void repl_trackable_update(void *self, void *ctx)
{
    g_trackable = self; // refreshed each frame
    if (g_orig_trackable_update)
        g_orig_trackable_update(self, ctx);

    // In-context capture: drain() must not read the position in the pre-World::Update window.
    float p[3];
    if (pal::read_player_position(self, p) && std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]))
    {
        g_last_pos = YcVec3{p[0], p[1], p[2]};
        g_have_pos = true;
    }
}

void repl_on_pickup_done(int slot, int item_type, void *player, void *vec, int a5, int a6, unsigned int a7, bool a8)
{
    g_player = player; // refresh the grant-target player for inbound replays
    if (g_orig_on_pickup_done)
        g_orig_on_pickup_done(slot, item_type, player, vec, a5, a6, a7, a8);
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
        pickup_item_type(self) = kApDummyItemType; // AP dummy: no-op grant + AP visual
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
            const int kind = native_location_kind(loc_idx);
            if (g_set_item_collected != nullptr && is_durable_bit_kind(kind))
            {
                g_set_item_collected(loc_idx, true, nullptr, nullptr);
                pal::logf(pal::LogLevel::Info, "outbound: SetItemCollected(locIdx=%d kind=%d) -> native reload suppression", loc_idx, kind);
            }
        }
    }
    if (g_orig_pickup_on_pickup)
        g_orig_pickup_on_pickup(self, listener);
}

// ShopMenu field offsets (build-specific; verified by self-check in repl_shop_item_present).
constexpr std::ptrdiff_t kShopLocIdxOff = 0x218;
constexpr std::ptrdiff_t kShopItemTypeOff = 0x21c;

pal::HookId g_id_shop_item_present = pal::kInvalidHookId;
void (*g_orig_shop_item_present)(void *) = nullptr;

void repl_shop_item_present(void *self)
{
    if (g_shop_offsets_ok && g_bridge != nullptr)
    {
        int &loc_idx = *reinterpret_cast<int *>(static_cast<char *>(self) + kShopLocIdxOff);
        int &item_type = *reinterpret_cast<int *>(static_cast<char *>(self) + kShopItemTypeOff);
        pal::logf(pal::LogLevel::Debug, "ShopMenu::ItemPresent locIdx=%d itemType=%d", loc_idx, item_type);

        // Offset self-check: garbage reads mean the ShopMenu layout shifted; disable the redirect.
        if (loc_idx < -1 || loc_idx >= kLocationCount || item_type < 0 || item_type >= kItemTypeCount)
        {
            g_shop_offsets_ok = false;
            pal::logf(pal::LogLevel::Error, "ShopMenu offset check FAILED (locIdx=%d itemType=%d); shop redirect disabled", loc_idx, item_type);
        }
        else if (g_bridge->is_ap_location(loc_idx))
        {
            pal::logf(pal::LogLevel::Info, "outbound: bought AP shop item locIdx=%d", loc_idx);
            g_bridge->on_location_collected(loc_idx);

            const int kind = native_location_kind(loc_idx);
            if (g_set_item_collected != nullptr && is_durable_bit_kind(kind))
            {
                g_set_item_collected(loc_idx, true, nullptr, nullptr);
                pal::logf(pal::LogLevel::Info, "outbound: SetItemCollected(locIdx=%d kind=%d) -> native reload suppression", loc_idx, kind);
            }

            item_type = kApDummyItemType; // suppress vanilla grant; real item arrives via AP inbound granter
        }
    }
    if (g_orig_shop_item_present)
        g_orig_shop_item_present(self);
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
constexpr std::ptrdiff_t kSaveSlotPtrOff = 0x18;      // active SaveSlot* = *(g_saveManager+0x18)
constexpr std::ptrdiff_t kSaveBlockUnlockOff = 0x200; // u64 lock-unlocked bitfield in the SaveSlot
constexpr std::ptrdiff_t kCollectionBitIdxOff = 0x1c; // uint8 bit index within that u64, per slot

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
    if (g_s_r_item_collection == 0 || self == nullptr)
        return -1;
    const auto *ic = reinterpret_cast<const unsigned char *>(g_s_r_item_collection);

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
    for (int i = 0; i < 0x168; ++i)
    {
        if (*reinterpret_cast<const std::uint64_t *>(ic + static_cast<std::size_t>(i) * kCollectionEntryStride) == key)
        {
            matched = i;
            break;
        }
    }
    if (matched < 0)
        return -1;

    // Apply the +0x4c warp remap; if no remap (<0), the matched index itself is the slot.
    const int warp = *reinterpret_cast<const int *>(ic + static_cast<std::size_t>(matched) * kCollectionEntryStride + 0x4c);
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

    g_id =
        hook_by_symbol(sym::on_pickup_done, reinterpret_cast<void *>(&repl_on_pickup_done), reinterpret_cast<void **>(&g_orig_on_pickup_done), "OnPickupDone");
    if (g_id == pal::kInvalidHookId)
    {
        g_bridge = nullptr;
        return;
    }
    installed_ = true;

    // Player* and position plumbing for inbound grants. Best-effort: missing symbols log and skip.
    g_id_player_ctor =
        hook_by_symbol(sym::player_ctor, reinterpret_cast<void *>(&repl_player_ctor), reinterpret_cast<void **>(&g_orig_player_ctor), "Player::Player");
    g_id_trackable_update = hook_by_symbol(sym::player_trackable_update, reinterpret_cast<void *>(&repl_trackable_update),
                                           reinterpret_cast<void **>(&g_orig_trackable_update), "PlayerTrackable::Update");

    g_s_r_items = pal::resolve_game_symbol(sym::s_r_items);
    if (g_s_r_items == 0)
        pal::logf(pal::LogLevel::Warn, "RandoHooks: s_rItems not found; item kind will log as -1 (grants still work)");
    repurpose_dummy_item();

    g_queue_destroy = reinterpret_cast<void (*)(void *, void *, bool)>(pal::resolve_game_symbol(sym::queue_destroy));
    if (g_queue_destroy == nullptr)
        pal::logf(pal::LogLevel::Warn, "RandoHooks: ycWorld::QueueDestroy not resolved; checked AP pickups will respawn");

    g_set_item_collected = reinterpret_cast<void (*)(int, bool, void *, void *)>(pal::resolve_game_symbol(sym::set_item_collected));
    g_s_r_item_collection = pal::resolve_game_symbol(sym::s_r_item_collection);
    if (g_set_item_collected == nullptr || g_s_r_item_collection == 0)
        pal::logf(pal::LogLevel::Warn, "RandoHooks: SetItemCollected/s_rItemCollection not resolved; bitfield-kind reload suppression disabled");

    g_id_pickup_init =
        hook_by_symbol(sym::pickup_init, reinterpret_cast<void *>(&repl_pickup_init), reinterpret_cast<void **>(&g_orig_pickup_init), "Pickup::Init");
    g_id_pickup_on_pickup = hook_by_symbol(sym::pickup_on_pickup, reinterpret_cast<void *>(&repl_pickup_on_pickup),
                                           reinterpret_cast<void **>(&g_orig_pickup_on_pickup), "Pickup::OnPickup");
    g_id_shop_item_present = hook_by_symbol(sym::shop_item_present, reinterpret_cast<void *>(&repl_shop_item_present),
                                            reinterpret_cast<void **>(&g_orig_shop_item_present), "ShopMenu::ItemPresent");
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
    if (g_id_trackable_update != pal::kInvalidHookId)
        pal::hook_engine().remove_hook(g_id_trackable_update);
    if (g_id_player_ctor != pal::kInvalidHookId)
        pal::hook_engine().remove_hook(g_id_player_ctor);
    if (g_id_pickup_init != pal::kInvalidHookId)
        pal::hook_engine().remove_hook(g_id_pickup_init);
    if (g_id_pickup_on_pickup != pal::kInvalidHookId)
        pal::hook_engine().remove_hook(g_id_pickup_on_pickup);
    if (g_id_shop_item_present != pal::kInvalidHookId)
        pal::hook_engine().remove_hook(g_id_shop_item_present);
    if (g_id_boss_trigger_death != pal::kInvalidHookId)
        pal::hook_engine().remove_hook(g_id_boss_trigger_death);
    if (g_id_boss_on_defeated != pal::kInvalidHookId)
        pal::hook_engine().remove_hook(g_id_boss_on_defeated);
    if (g_id_key_block_update != pal::kInvalidHookId)
        pal::hook_engine().remove_hook(g_id_key_block_update);
    g_logged_lock_slots.clear();
    g_locks = nullptr;
    g_seed_logged = false;
    if (installed_)
        pal::hook_engine().remove_hook(g_id);
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
    if (g_save_manager == 0 || g_s_r_item_collection == 0)
        return;
    void *saveslot = *reinterpret_cast<void **>(g_save_manager + kSaveSlotPtrOff);
    if (saveslot == nullptr)
        return; // no active save (e.g. title/menus)

    const std::vector<int> slots = locks_.removed_slots();
    if (slots.empty())
        return;

    auto &field = *reinterpret_cast<std::uint64_t *>(static_cast<char *>(saveslot) + kSaveBlockUnlockOff);
    for (int slot : slots)
    {
        if (slot < 0 || slot >= kLocationCount)
            continue;
        const std::uint8_t bit =
            *reinterpret_cast<std::uint8_t *>(g_s_r_item_collection + static_cast<std::uintptr_t>(slot) * kCollectionEntryStride + kCollectionBitIdxOff);
        field |= (std::uint64_t{1} << bit);
    }

    if (!g_seed_logged)
    {
        g_seed_logged = true;
        pal::logf(pal::LogLevel::Info, "locks: seeded %zu removed lock(s) into SaveSlot unlock bitfield", slots.size());
    }
}

bool GameItemGranter::grant(int item_type)
{
    // Require hook + Player* + trackable before accepting; return false to retry next tick.
    if (g_orig_on_pickup_done == nullptr || g_player == nullptr || g_trackable == nullptr)
        return false;

    // itemType 0 = engine "None" sentinel; treat as handled so the cursor advances.
    if (item_type <= 0)
        return true;

    std::lock_guard<std::mutex> lk(g_pending_mtx);
    g_pending.push_back(item_type);
    return true;
}

void GameItemGranter::drain()
{
    std::vector<int> batch;
    {
        std::lock_guard<std::mutex> lk(g_pending_mtx);
        if (g_pending.empty())
            return;
        batch.swap(g_pending);
    }

    // Position comes from the cache captured in PlayerTrackable::Update (in-update
    // context); calling GetPos here (pre-World::Update spawn window) walks a camera
    // graph that is not yet valid and faults. Requeue until a position is cached.
    const bool ready = g_orig_on_pickup_done && g_player && g_have_pos;
    const YcVec3 pos = ready ? g_last_pos : YcVec3{};
    if (!ready || !std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z))
    {
        std::lock_guard<std::mutex> lk(g_pending_mtx);
        g_pending.insert(g_pending.begin(), batch.begin(), batch.end());
        return;
    }

    // locIdx=-1: grant by type only, no location state touched.
    for (int item_type : batch)
    {
        YcVec3 grant_pos = pos; // fresh copy per item (ycVec3 const&)
        g_orig_on_pickup_done(-1, item_type, g_player, &grant_pos, 0, 0, 0, false);
        pal::logf(pal::LogLevel::Info, "inbound: granted item_type=%d (kind=%d)", item_type, storage_kind(item_type));
    }
}

} // namespace mth
