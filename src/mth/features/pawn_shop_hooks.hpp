#pragma once

#include <functional>

namespace mth
{

// Owns the PawnShopNPC::OnNPCEvent suppressor that makes the pawn shop ("Pawnty") uninteractible
// while should_disable() is true. Installs on construction, removes on destruction.
class PawnShopHooks
{
  public:
    explicit PawnShopHooks(std::function<bool()> should_disable);
    ~PawnShopHooks();

    PawnShopHooks(const PawnShopHooks &) = delete;
    PawnShopHooks &operator=(const PawnShopHooks &) = delete;
};

} // namespace mth
