#include "mth/features/location_hooks.hpp"

#include <bit>
#include <cstdint>
#include <functional>

#include "mod/mod_api.hpp"
#include "mth/core/ap/ap_ids.hpp"
#include "mth/core/data/game_layout.hpp"
#include "mth/core/data/game_symbols.hpp"
#include "mth/core/data/game_tables.hpp"
#include "mth/core/rando_bridge.hpp"
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
// lockstep to cancel the free key. The game reads usable keys from the SaveSlot (KeyBlock::Update,
// PlayerGetKeysSpent); there is no Player-side key mirror to update (see game_layout.hpp). Delta-based so a
// re-collected (already-set) bit is a no-op and can't drive usable keys negative.
void neutralize_kear_grant(int loc_idx, void *slot, std::uint64_t before)
{
    auto &bits = *reinterpret_cast<std::uint64_t *>(static_cast<char *>(slot) + mth::layout::kSaveKearBitsOff);
    const std::uint64_t new_bits = bits & ~before;
    if (new_bits == 0)
        return; // bit already collected; no key was granted, nothing to neutralize

    const int n = std::popcount(new_bits);
    *reinterpret_cast<int *>(static_cast<char *>(slot) + mth::layout::kSaveKearSpentOff) += n;
    pal::logf(pal::LogLevel::Info, "kear_rando: neutralized kear grant locIdx=%d new_bits=%d spent+=%d", loc_idx, n, n);
}

// Reload-durable counterpart to neutralize_kear_grant: the collect-time spent bump above is not rebuilt on
// reload (only the +0x1f0 collected bitfield is), so after loading a save the AP-collected kears read as
// usable keys again ("one kear on load"). Run every tick under kear_rando to raise +0x1f8 back up to
// popcount(+0x1f0), cancelling the leaked keys; never lowers it, so real lock-spends survive. SaveSlot is the
// authoritative usable-key source (no Player mirror exists -- see neutralize_kear_grant).
void reconcile_kear_keys()
{
    if (!g_kear_rando || g_save_manager == 0)
        return;
    void *slot = pal::active_save_slot(g_save_manager);
    if (slot == nullptr)
        return;
    const std::uint64_t bits = *reinterpret_cast<std::uint64_t *>(static_cast<char *>(slot) + mth::layout::kSaveKearBitsOff);
    int &spent = *reinterpret_cast<int *>(static_cast<char *>(slot) + mth::layout::kSaveKearSpentOff);
    const int reconciled = mth::tables::kear_reconciled_spent(bits, spent);
    if (reconciled == spent)
        return; // already balanced; no leaked key
    pal::logf(pal::LogLevel::Info, "kear_rando: reconciled spent %d -> %d (popcount=%d) on slot=%p", spent, reconciled, std::popcount(bits), slot);
    spent = reconciled;
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

void (*g_orig_pickup_init)(void *, int, int, bool) = nullptr;

void repl_pickup_init(void *self, int item_type, int loc_idx, bool flag)
{
    if (g_orig_pickup_init)
        g_orig_pickup_init(self, item_type, loc_idx, flag);

    // Offset self-check: if the entity doesn't store loc_idx at our offset, layout shifted; disable redirect.
    if (g_pickup_offsets_ok && pickup_loc_idx(self) != loc_idx)
    {
        g_pickup_offsets_ok = false;
        pal::logf(pal::LogLevel::Error, "Pickup offset check FAILED (stored=%d arg=%d); outbound redirect disabled", pickup_loc_idx(self), loc_idx);
        return;
    }
    if (!g_pickup_offsets_ok || g_bridge == nullptr)
        return;

    // #67: the WeaponMerchant (Legovich) forge spawns the forged-weapon Pickup with loc_idx == the pending
    // weapon index SaveSlot+0xc70. The mod suppresses that grant, so the vanilla OnPickupDone `if(c70==loc)
    // c70=-1` reset never runs and c70 sticks on the checked AP weapon location, leaving the merchant
    // uninteractable. Clear it here -- after g_orig_pickup_init built the pickup (above), so the forge asset is
    // resolved and the merchant can't re-spawn with itemType 0 (clearing c70 before that spawn freezes).
    void *wsave = g_save_manager != 0 ? pal::active_save_slot(g_save_manager) : nullptr;
    const int weapon_idx = wsave != nullptr ? *reinterpret_cast<int *>(static_cast<char *>(wsave) + mth::layout::kSaveWeaponIndexOff) : -1;
    if (weapon_idx >= 0 && loc_idx == weapon_idx)
    {
        *reinterpret_cast<int *>(static_cast<char *>(wsave) + mth::layout::kSaveWeaponIndexOff) = -1;
        *reinterpret_cast<unsigned char *>(static_cast<char *>(wsave) + mth::layout::kSaveWeaponMoldLatchOff) = 0;
        pal::logf(pal::LogLevel::Info, "legovich: forge pickup loc=%d -> reset weapon mold to re-arm merchant (#67)", loc_idx);
    }

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

    // #93: for a have-bit location (kinds 1/9/11) the game self-kills the box when IsItemCollected reports the
    // item owned, which any AP grant of that item does while the location is still unchecked. The IsItemCollected
    // hook redirects that read, but MSVC inlines IsItemCollected into the Pickup ctor's copy of the self-kill,
    // out of the hook's reach (GCC keeps a real call, so Linux is unaffected). The ctor arms the kill via
    // Pickup+0x3ac; clearing it for these locations skips the inlined gate. Checked ones returned via QueueDestroy above.
    if (g_bridge->is_ap_location(loc_idx) && mth::tables::is_item_keyed_collected_kind(mth::tables::native_location_kind(loc_idx)))
        *reinterpret_cast<unsigned char *>(static_cast<char *>(self) + mth::layout::kPickupSaveTrackedFlagOff) = 0;

    // The dummy-itemType redirect is deferred to repl_pickup_on_pickup, not done here: the Pickup ctor's
    // surprise-spawn emerge block runs AFTER Init and reads s_rItems[storedType].kind to decide whether to
    // detach the revealed pickup from the dying wall (SpawnPoint::DisownParent); the dummy's kind 0 misses
    // that branch, so a hidden-room kear stays attached and invisible until a room reload (#86). Init builds
    // the sprite from the real itemType regardless, so deferring changes no visual.
    if (g_bridge->is_ap_location(loc_idx))
        pal::logf(pal::LogLevel::Debug, "Pickup::Init locIdx=%d itemType=%d -> AP location (dummy redirect deferred to collect)", loc_idx, item_type);
    else
        pal::logf(pal::LogLevel::Debug, "Pickup::Init locIdx=%d itemType=%d -> vanilla (not an AP location)", loc_idx, item_type);
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
            // No-op the vanilla grant: deferred from Pickup::Init (see #86). The vanilla OnPickup below re-reads
            // this field live, so OnPickupDone still receives the dummy (kind 0 -> no grant).
            pickup_item_type(pickup) = mth::layout::kApDummyItemType;
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
// state instead - advancing tiered slots past bought levels and selling out only when all are checked
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
// `ownership_query` is IsItemCollected's param5 (b5): the weapon-swap chest reads weapon ownership via
// IsItemCollected with b5=true, and must see the real have-item bit - redirecting it to the location's
// AP checked-state hides any weapon received from another player (its own location never checked). So
// ownership queries on weapon-kind (1) locations pass through; see should_redirect_collected_query.
// SaveSlot+0xc70: the WeaponMerchant's pending weapon-upgrade index (-1 = none / save unavailable).
[[nodiscard]] int current_weapon_index()
{
    if (g_save_manager == 0)
        return -1;
    void *slot = pal::active_save_slot(g_save_manager);
    return slot != nullptr ? *reinterpret_cast<int *>(static_cast<char *>(slot) + mth::layout::kSaveWeaponIndexOff) : -1;
}

// Set while Shop::IsOutOfStock runs (game thread; a depth counter survives any re-entry). Lets the
// IsItemCollected override below tell the WeaponMerchant's stock tally apart from every other reader of the
// same weapon locations -- chiefly the weapon-swap chest, which must keep seeing the real have-item bit.
int g_shop_oos_depth = 0;
std::uint64_t (*g_orig_shop_oos)(void *, void *) = nullptr;

std::uint64_t repl_shop_is_out_of_stock(void *shop_def, void *out_info)
{
    ++g_shop_oos_depth;
    const std::uint64_t r = g_orig_shop_oos != nullptr ? g_orig_shop_oos(shop_def, out_info) : 0;
    --g_shop_oos_depth;
    return r;
}

int on_item_collected_query(int loc_idx, bool ownership_query)
{
    if (g_bridge == nullptr)
        return -1;
    // Legovich (#67 follow-up): the WeaponMerchant's out-of-stock tally must count AP purchases, not weapons
    // received from the multiworld -- otherwise a received weapon inflates the count and Armand arms early.
    // Report AP-checked-state for his slots, but ONLY while his shop is the one tallying stock; the weapon-swap
    // chest reads these same locations and must see the real have-item bit, so it (b5 ownership query, not under
    // IsOutOfStock) falls through to the normal path below. EXCLUDE the slot currently being forged
    // (SaveSlot+0xc70 == loc): vanilla counts the in-progress slot once via its "+1 for pending" term, and that
    // slot is already AP-checked at buy-confirm, so counting it here too double-counts the first buy -> Armand
    // after one purchase.
    if (g_shop_oos_depth > 0 && mth::is_legovich_location(loc_idx) && g_bridge->is_ap_location(loc_idx))
        return (g_bridge->is_checked(loc_idx) && loc_idx != current_weapon_index()) ? 1 : 0;
    const bool is_cap = mth::tables::is_capacity_upgrade_location(loc_idx);
    const int kind = is_cap ? -1 : mth::tables::native_location_kind(loc_idx); // kind unused when capacity
    if (!mth::tables::should_redirect_collected_query(is_cap, kind, ownership_query))
        return -1; // not an aliasing location, or a weapon ownership query -> pass through
    if (!g_bridge->is_ap_location(loc_idx))
        return -1;
    return g_bridge->is_checked(loc_idx) ? 1 : 0;
}

} // namespace

namespace mth
{

LocationHooks::LocationHooks(RandoBridge &bridge)
{
    g_bridge = &bridge;
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
    shop_oos_ = ScopedHook(sym::shop_is_out_of_stock, reinterpret_cast<void *>(&repl_shop_is_out_of_stock), reinterpret_cast<void **>(&g_orig_shop_oos),
                           "Shop::IsOutOfStock");
    pal::install_shop_purchase_hook(&on_shop_buy);
    pal::install_shop_stock_hook(&on_shop_stock);
    mod::install_item_collected_hook(&on_item_collected_query);
}

void LocationHooks::set_kear_rando(bool on)
{
    g_kear_rando = on;
}

void LocationHooks::reconcile_kear_keys()
{
    ::reconcile_kear_keys();
}

LocationHooks::~LocationHooks()
{
    mod::remove_item_collected_hook();
    pal::remove_shop_stock_hook();
    pal::remove_shop_purchase_hook();
    // g_bridge nulled before the ScopedHook members remove the detours; the repls null-check it.
    g_bridge = nullptr;
}

} // namespace mth
