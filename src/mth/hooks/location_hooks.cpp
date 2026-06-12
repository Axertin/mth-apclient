#include "mth/hooks/location_hooks.hpp"

#include <cstdint>

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

// Shared collect path for pickups and shop buys: record/send the check, then write the
// durable native collected-bit where SetItemCollected is side-effect-free.
void collect_ap_location(int loc_idx)
{
    g_bridge->on_location_collected(loc_idx); // mark_checked, persist, send if connected

    const int kind = mth::tables::native_location_kind(loc_idx);
    if (g_set_item_collected != nullptr && mth::tables::is_durable_bit_kind(kind))
    {
        g_set_item_collected(loc_idx, true, nullptr, nullptr);
        pal::logf(pal::LogLevel::Info, "outbound: SetItemCollected(locIdx=%d kind=%d) -> native reload suppression", loc_idx, kind);
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
        return mth::layout::kApDummyItemType; // suppress vanilla grant; real item arrives via AP inbound granter
    }
    return item_type;
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

    pickup_init_ = ScopedHook(sym::pickup_init, reinterpret_cast<void *>(&repl_pickup_init), reinterpret_cast<void **>(&g_orig_pickup_init), "Pickup::Init");
    pickup_on_pickup_ = ScopedHook(sym::pickup_on_pickup, reinterpret_cast<void *>(&repl_pickup_on_pickup), reinterpret_cast<void **>(&g_orig_pickup_on_pickup),
                                   "Pickup::OnPickup");
    pal::install_shop_purchase_hook(&on_shop_buy);
}

LocationHooks::~LocationHooks()
{
    pal::remove_shop_purchase_hook();
    // g_bridge nulled before the ScopedHook members remove the detours; the repls null-check it.
    g_bridge = nullptr;
}

} // namespace mth
