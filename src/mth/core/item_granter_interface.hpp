#pragma once

namespace mth
{

// twin: mth/features/item_granter.hpp is the game-coupled impl.
// Grants one item by game itemType. Returns false if unavailable now; caller retries without marking.
class IItemGranter
{
  public:
    virtual ~IItemGranter() = default;
    virtual bool grant(int item_type) = 0;
};

} // namespace mth
