#pragma once

namespace mth
{

class RandoBridge;

// Installs the item-collection detours: the inbound OnPickupDone replay plumbing
// (Player/trackable), the Pickup::Init spawn-redirect (AP-owned locations show the
// sentinel), and the Pickup::OnPickup collect-detect (sends the location check).
// Each is resolved by symbol; any not found is logged and skipped. One instance, owned by App.
class RandoHooks
{
  public:
    RandoHooks(RandoBridge &bridge);
    ~RandoHooks();
    RandoHooks(const RandoHooks &) = delete;
    RandoHooks &operator=(const RandoHooks &) = delete;

  private:
    bool installed_{false};
};

} // namespace mth
