#pragma once

#include <functional>

namespace mth
{

class ApState;
class ApSaveState;
class IItemGranter;

// Drives inbound grants each tick. Stops at first granter failure and retries next tick.
class InboundGranter
{
  public:
    // credit_kear_key: vanilla-kear-mode effect that grants one usable key (game-memory write, injected
    // by App). Returns false when no save/player is live yet, so the receipt retries next tick unmarked.
    // Empty for offline/tests that never receive a vanilla kear.
    InboundGranter(IItemGranter &granter, ApState &state, ApSaveState &save, std::function<bool()> credit_kear_key = {});
    void tick();

  private:
    IItemGranter &granter_;
    ApState &state_;
    ApSaveState &save_;
    std::function<bool()> credit_kear_key_;
};

} // namespace mth
