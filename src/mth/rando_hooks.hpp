#pragma once

namespace mth
{

class RandoBridge;

// Installs Pickup::Init (spawn redirect/despawn), Pickup::OnPickup (collect detect),
// and OnPickupDone/Player plumbing for inbound grants. Symbols resolved at construction.
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
