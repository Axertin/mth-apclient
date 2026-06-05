#pragma once

#include "mth/core/item_granter.hpp"

namespace mth
{

// IItemGranter via Items::OnPickupDone replay. grant() enqueues (false until Player* seen);
// drain() replays from the engine's spawn window (pre-World::Update).
class GameItemGranter final : public IItemGranter
{
  public:
    bool grant(int item_type) override;
    void drain(); // run from a spawn-safe game-thread hook; replays queued OnPickupDone grants
};

} // namespace mth
