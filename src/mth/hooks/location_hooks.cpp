#include "mth/hooks/location_hooks.hpp"

#include <bit>
#include <cstdint>
#include <functional>

#include "mth/core/game_layout.hpp"
#include "mth/core/game_symbols.hpp"
#include "mth/core/rando_bridge.hpp"
#include "mth/hooks/game_tables.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

namespace
{

mth::RandoBridge *g_bridge = nullptr;

// ycWorld::QueueDestroy: tears down a pickup entity; used for already-checked locations not covered by the native gate.
void (*g_queue_destroy)(void *, void *, bool) = nullptr;

// SetItemCollected: writes a durable bitfield bit so the native Pickup::Init reload gate suppresses respawn (bitfield kinds only).
void (*g_set_item_collected)(int, bool, void *, void *) = nullptr;

// g_saveManager global (resolves the active SaveSlot); needed to neutralize the kear grant under kear_rando.
std::uintptr_t g_save_manager = 0;

// Returns the live Player* (or nullptr) so the kear neutralization can keep the live +0x1190/+0x1198
// mirrors in sync with the SaveSlot fields; supplied by App from the PlayerTracker.
std::function<void *()> g_player_get;

// slot_data "kear_rando": kears are AP-randomized, so the SaveSlot+0x1f0 bit a kear collect sets must not
// count as a usable key. Game-thread only (set from drive_tick, read from the pickup/shop collect path).
bool g_kear_rando = false;

constexpr int kKearStorageKind = 8; // s_rItems kind 8: kear/key items; their collected-bit IS the usable-key bit (+0x1f0).

// Offset self-checks (build drift): cleared on first mismatch, disabling the feature loudly.
bool g_pickup_offsets_ok = true;
bool g_shop_offsets_ok = true;

[[nodiscard]] int &pickup_loc_idx(void *self)
{
    return *reinterpret_cast<int *>(static_cast<char *>(self) + mth::layout::kPickupLocIdxOff);
}
[[nodiscard]] int &pickup_item_type(void *self)
{
    return *reinterpret_cast<int *>(static_cast<char *>(self) + mth::layout::kPickupItemTypeOff);
}

// A kear collect records its bit in the SaveSlot+0x1f0 bitfield, which doubles as the usable-key count
// (usable = popcount(+0x1f0) - spent(+0x1f8)). Under kear_rando the key is AP-controlled, so for each bit
// the just-run SetItemCollected actually NEWLY set (`new_bits`, vs `before`), bump the spent-counter in
// lockstep to cancel the free key. The live lock gate reads the Player+0x1190/+0x1198 mirrors instead of the
// SaveSlot, so mirror the same delta there too; otherwise usable would read one low until a reload re-syncs.
// Delta-based so a re-collected (already-set) bit is a no-op and can't drive usable keys negative.
void neutralize_kear_grant(int loc_idx, void *slot, std::uint64_t before)
{
    auto &bits = *reinterpret_cast<std::uint64_t *>(static_cast<char *>(slot) + mth::layout::kSaveKearBitsOff);
    const std::uint64_t new_bits = bits & ~before;
    if (new_bits == 0)
        return; // bit already collected; no key was granted, nothing to neutralize

    const int n = std::popcount(new_bits);
    *reinterpret_cast<int *>(static_cast<char *>(slot) + mth::layout::kSaveKearSpentOff) += n;

    void *player = g_player_get ? g_player_get() : nullptr;
    if (player != nullptr)
    {
        *reinterpret_cast<std::uint64_t *>(static_cast<char *>(player) + mth::layout::kPlayerKearBitsOff) |= new_bits;
        *reinterpret_cast<int *>(static_cast<char *>(player) + mth::layout::kPlayerKearSpentOff) += n;
    }
    pal::logf(pal::LogLevel::Info, "kear_rando: neutralized kear grant locIdx=%d new_bits=%d spent+=%d player=%p", loc_idx, n, n, player);
}

// Shared collect path for pickups and shop buys: record/send the check, then write the
// durable native collected-bit where SetItemCollected is side-effect-free.
void collect_ap_location(int loc_idx)
{
    g_bridge->on_location_collected(loc_idx); // mark_checked, persist, send if connected

    const int kind = mth::tables::native_location_kind(loc_idx);
    if (g_set_item_collected != nullptr && mth::tables::is_durable_bit_kind(kind))
    {
        // Kear (kind 8): capture the bitfield before the write so neutralization targets exactly the new bit.
        const bool neutralize = g_kear_rando && kind == kKearStorageKind && g_save_manager != 0;
        void *slot = neutralize ? pal::active_save_slot(g_save_manager) : nullptr;
        const std::uint64_t before = slot != nullptr ? *reinterpret_cast<std::uint64_t *>(static_cast<char *>(slot) + mth::layout::kSaveKearBitsOff) : 0;

        g_set_item_collected(loc_idx, true, nullptr, nullptr);
        pal::logf(pal::LogLevel::Info, "outbound: SetItemCollected(locIdx=%d kind=%d) -> native reload suppression", loc_idx, kind);

        if (neutralize && slot != nullptr)
            neutralize_kear_grant(loc_idx, slot, before); // kear collected-bit == usable key; cancel it (AP controls keys)
        else if (neutralize)
            pal::logf(pal::LogLevel::Warn, "kear_rando: no active save slot; kear grant NOT neutralized (locIdx=%d)", loc_idx);
    }
}

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
    if (g_queue_destroy != nullptr && g_bridge->is_checked(loc_idx))
    {
        void *ent = *reinterpret_cast<void **>(static_cast<char *>(self) + mth::layout::kComponentEntityOff);
        void *world = ent != nullptr ? *reinterpret_cast<void **>(static_cast<char *>(ent) + mth::layout::kEntityWorldOff) : nullptr;
        if (ent != nullptr && world != nullptr)
        {
            *reinterpret_cast<unsigned *>(static_cast<char *>(self) + mth::layout::kPickupKilledFlagOff) |= 1u; // killed flag
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

void (*g_orig_pickup_on_pickup)(void *, void *) = nullptr;

void repl_pickup_on_pickup(void *self, void *listener)
{
    // `self` may be an MI subobject, not the Pickup base; recover the base for the loc_idx
    // read. The trampoline still gets the original `self`.
    void *pickup = pal::pickup_base_from_onpickup(self);

    if (g_pickup_offsets_ok && g_bridge != nullptr)
    {
        const int loc_idx = pickup_loc_idx(pickup);
        pal::logf(pal::LogLevel::Debug, "Pickup::OnPickup locIdx=%d", loc_idx);
        if (g_bridge->is_ap_location(loc_idx))
        {
            pal::logf(pal::LogLevel::Info, "outbound: collected AP location locIdx=%d", loc_idx);
            collect_ap_location(loc_idx);
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
        collect_ap_location(loc_idx);
        // Linux suppresses the vanilla grant via this returned dummy itemType; Windows can't redirect, so
        // the OnPickupDone detour skips the grant for AP locations instead. Real item arrives via AP inbound.
        return mth::layout::kApDummyItemType;
    }
    return item_type;
}

// ShopItem::Refresh level classifier: the PAL walks a slot's level chain and asks, per level loc_idx,
// whether it's an AP location and whether it's been checked. We suppress the vanilla grant for AP buys
// (the grant is what normally advances the slot / zeroes its stock), so the platform replays it from AP
// state instead -- advancing tiered slots past bought levels and selling out only when all are checked
// (issue #48). 0 = not an AP location, 1 = AP location not yet checked, 2 = AP location checked.
int on_shop_stock(int loc_idx)
{
    if (g_bridge == nullptr || !g_bridge->is_ap_location(loc_idx))
        return 0;
    return g_bridge->is_checked(loc_idx) ? 2 : 1;
}

// Items::IsItemCollected override for locations whose vanilla collected-state aliases item-ownership, so
// the game reports them collected before the player ever opens the chest:
//   - capacity-upgrade pieces (#8): the collected bit aliases the mod's AP capacity counter (apply_upgrades
//     sets low bits of SaveSlot+0x130 etc. for received Magic/Health/Spark/Vial/Trinket upgrades), so a
//     boss skips spawning the rose reward.
//   - have-item-bit kinds 1/9/11 (#61): IsItemCollected keys these on the item's identity/type, so an
//     out-of-order AP grant of the vanilla item (trinket, subweapon, vessel, ...) marks the location
//     collected and its chest spawns already-open, sending no check.
// For such AP locations report the AP checked-state; pass everything else through (-1). The cheap
// kind/itemType checks fast-reject the vast majority of (hot-path) queries before the AP-location lookup.
int on_item_collected_query(int loc_idx)
{
    if (g_bridge == nullptr)
        return -1;
    if (!mth::tables::is_capacity_upgrade_location(loc_idx) && !mth::tables::is_item_keyed_collected_kind(mth::tables::native_location_kind(loc_idx)))
        return -1; // not an aliasing location -> pass through to the original IsItemCollected
    if (!g_bridge->is_ap_location(loc_idx))
        return -1;
    return g_bridge->is_checked(loc_idx) ? 1 : 0;
}

} // namespace

namespace mth
{

LocationHooks::LocationHooks(RandoBridge &bridge, std::function<void *()> player_get)
{
    g_bridge = &bridge;
    g_player_get = std::move(player_get);
    tables::resolve();
    tables::repurpose_dummy_item();

    g_queue_destroy = reinterpret_cast<void (*)(void *, void *, bool)>(pal::resolve_game_symbol(sym::queue_destroy));
    if (g_queue_destroy == nullptr)
        pal::logf(pal::LogLevel::Warn, "LocationHooks: ycWorld::QueueDestroy not resolved; checked AP pickups will respawn");

    g_set_item_collected = reinterpret_cast<void (*)(int, bool, void *, void *)>(pal::resolve_game_symbol(sym::set_item_collected));
    if (g_set_item_collected == nullptr || !tables::collection_resolved())
        pal::logf(pal::LogLevel::Warn, "LocationHooks: SetItemCollected/s_rItemCollection not resolved; bitfield-kind reload suppression disabled");

    g_save_manager = pal::resolve_game_symbol(sym::save_manager);
    if (g_save_manager == 0)
        pal::logf(pal::LogLevel::Warn, "LocationHooks: g_saveManager not resolved; kear_rando key-grant neutralization disabled");

    pickup_init_ = ScopedHook(sym::pickup_init, reinterpret_cast<void *>(&repl_pickup_init), reinterpret_cast<void **>(&g_orig_pickup_init), "Pickup::Init");
    pickup_on_pickup_ = ScopedHook(sym::pickup_on_pickup, reinterpret_cast<void *>(&repl_pickup_on_pickup), reinterpret_cast<void **>(&g_orig_pickup_on_pickup),
                                   "Pickup::OnPickup");
    pal::install_shop_purchase_hook(&on_shop_buy);
    pal::install_shop_stock_hook(&on_shop_stock);
    pal::install_item_collected_hook(&on_item_collected_query);
}

void LocationHooks::set_kear_rando(bool on)
{
    g_kear_rando = on;
}

LocationHooks::~LocationHooks()
{
    pal::remove_item_collected_hook();
    pal::remove_shop_stock_hook();
    pal::remove_shop_purchase_hook();
    // g_bridge nulled before the ScopedHook members remove the detours; the repls null-check it.
    g_bridge = nullptr;
    g_player_get = nullptr;
}

} // namespace mth
