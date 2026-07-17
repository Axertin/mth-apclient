#pragma once

#include <functional>

#include "mth/hooks/scoped_hook.hpp"

namespace mth
{

class RandoBridge;
class ScoutRegistry;

// Outbound location checks from world pickups and shop buys: Pickup::Init (AP-dummy
// redirect + checked-location despawn), Pickup::OnPickup (collect detect), and the
// PAL shop-purchase hook. Also patches the AP dummy item row at construction.
class LocationHooks
{
  public:
    // `scout` is optional (nullable): when present, shop box selection is rewritten from scouted
    // AP data (name/desc/color); when null, the shop text hook is not installed and shop text stays
    // vanilla.
    explicit LocationHooks(RandoBridge &bridge, ScoutRegistry *scout = nullptr);
    ~LocationHooks();
    LocationHooks(const LocationHooks &) = delete;
    LocationHooks &operator=(const LocationHooks &) = delete;

    // Kear key gating per slot_data "kear_rando" mode. `neutralize` (all AP modes): cancel the free key a
    // kear-location collect would grant. `suppress` (AP-item modes only): pin usable keys to 0 via reconcile.
    void set_kear_gating(bool neutralize, bool suppress);

    // Live Player* accessor for the kear key edits (neutralize/credit must move the Player+0x11b0 spent
    // mirror alongside SaveSlot+0x1f8). Wired once by App via HookManager.
    void set_player_getter(std::function<void *()> get_player);

    // Vanilla kear mode (#130): grant one usable key for a received Universal Kear by lowering both the
    // SaveSlot and Player spent-counters. Returns false (retry) until a save + player are live. `player` is
    // the live Player* (from the tracker). Game-thread; idempotency is the caller's (per-receipt marker).
    bool credit_kear_key(void *player);

    // Reload-durable re-assertion of the kear key cancel: raise the SaveSlot spent-counter back up to
    // popcount so AP-collected kears stop reading as usable keys after a save load. Game-thread, per-tick.
    void reconcile_kear_keys();

    // Write the native durable collected-bit for server-collected (Collect/coop) durable-bit locations, so
    // their chests spawn opened like a live collect (reconcile alone only marks our .state). Game-thread,
    // per-tick; self-guards on an active save, so it no-ops at the title screen.
    void enforce_native_bits();

    // Re-arm enforce_native_bits after a world teardown (a save reload clears the game's collection of our
    // in-memory writes, so they must be re-applied on the next load).
    void reset_native_bits();

  private:
    ScopedHook pickup_init_;
    ScopedHook pickup_on_pickup_;
    ScopedHook shop_oos_; // brackets Shop::IsOutOfStock so the IsItemCollected override can scope to it (#67)
};

} // namespace mth
