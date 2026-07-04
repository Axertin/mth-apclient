#pragma once

#include <functional>

#include "mth/core/item_granter_interface.hpp"
#include "mth/hooks/scoped_hook.hpp"

namespace mth
{

class PlayerTracker;

// IItemGranter via Items::OnPickupDone replay. Owns the OnPickupDone hook (which also
// refreshes the tracker's Player* on every vanilla pickup). grant() enqueues (false
// until a Player* is captured); drain() replays from the engine's spawn window
// (pre-World::Update), requeueing until the tracker has a cached position.
class ItemGranter final : public IItemGranter
{
  public:
    // is_ap_location: RandoBridge predicate used to suppress vanilla grants for randomized locations.
    // report_collected: RandoBridge sink invoked when a suppressed grant IS the player collecting an AP
    // location through Items::OnPickupDone directly (no Pickup entity / ShopMenu) -- e.g. the train-ticket
    // machine; sends the outbound check that the pickup/shop hooks would otherwise miss (idempotent).
    ItemGranter(PlayerTracker &tracker, std::function<bool(int)> is_ap_location, std::function<void(int)> report_collected);
    ~ItemGranter() override;
    ItemGranter(const ItemGranter &) = delete;
    ItemGranter &operator=(const ItemGranter &) = delete;

    bool grant(int item_type) override;
    void drain();

  private:
    ScopedHook pickup_done_;
    ScopedHook pickup_; // Items::OnPickup: suppress armor upgrades (0x4f/0x50) that grant before OnPickupDone (#71)
};

} // namespace mth
