#pragma once

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

  private:
    ScopedHook pickup_init_;
    ScopedHook pickup_on_pickup_;
};

} // namespace mth
