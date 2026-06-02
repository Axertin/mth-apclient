#pragma once

namespace mth
{

class RandoBridge;

// Installs the item-collection detours (read-only MVP: OnPickupDone only).
// Resolves OnPickupDone by symbol; logs and installs nothing if not found.
// One instance, owned by App.
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
