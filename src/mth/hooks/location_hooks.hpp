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
    // player_get returns the live Player* (or nullptr) for the kear-grant live-mirror sync.
    LocationHooks(RandoBridge &bridge, std::function<void *()> player_get);
    ~LocationHooks();
    LocationHooks(const LocationHooks &) = delete;
    LocationHooks &operator=(const LocationHooks &) = delete;

    // slot_data "kear_rando": when on, neutralize the usable key a kear-location collect would otherwise grant.
    void set_kear_rando(bool on);

    // Reload-durable re-assertion of the kear key cancel: raise the SaveSlot spent-counter back up to
    // popcount so AP-collected kears stop reading as usable keys after a save load. Game-thread, per-tick.
    void reconcile_kear_keys();

  private:
    ScopedHook pickup_init_;
    ScopedHook pickup_on_pickup_;
};

} // namespace mth
