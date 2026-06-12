#pragma once

#include "mth/core/item_granter.hpp"
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
    explicit ItemGranter(PlayerTracker &tracker);
    ~ItemGranter() override;
    ItemGranter(const ItemGranter &) = delete;
    ItemGranter &operator=(const ItemGranter &) = delete;

    bool grant(int item_type) override;
    void drain();

  private:
    ScopedHook pickup_done_;
};

} // namespace mth
