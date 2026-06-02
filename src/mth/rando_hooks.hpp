#pragma once

#include "mth/core/build_id.hpp"

namespace mth
{

class RandoBridge;

// Installs the item-collection detours (read-only MVP: OnPickupDone only).
// Build-id gated: an unmapped build installs nothing. One instance, owned by App.
class RandoHooks
{
  public:
    RandoHooks(Build build, RandoBridge &bridge);
    ~RandoHooks();
    RandoHooks(const RandoHooks &) = delete;
    RandoHooks &operator=(const RandoHooks &) = delete;

  private:
    bool installed_{false};
};

} // namespace mth
