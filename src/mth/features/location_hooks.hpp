#pragma once

#include <functional>

#include "mth/hooks/scoped_hook.hpp"

namespace mth
{

class RandoBridge;

// Outbound location checks from world pickups and shop buys: Pickup::Init (AP-dummy
// redirect + checked-location despawn), Pickup::OnPickup (collect detect), and the
// PAL shop-purchase hook. Also patches the AP dummy item row at construction.
class LocationHooks
{
  public:
    explicit LocationHooks(RandoBridge &bridge);
    ~LocationHooks();
    LocationHooks(const LocationHooks &) = delete;
    LocationHooks &operator=(const LocationHooks &) = delete;

    // slot_data "kear_rando": when on, neutralize the usable key a kear-location collect would otherwise grant.
    void set_kear_rando(bool on);

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
