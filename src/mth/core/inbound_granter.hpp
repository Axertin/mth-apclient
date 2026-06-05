#pragma once

namespace mth
{

class ApState;
class ApSaveState;
class IItemGranter;

// Drives inbound grants each tick. Stops at first granter failure and retries next tick.
class InboundGranter
{
  public:
    InboundGranter(IItemGranter &granter, ApState &state, ApSaveState &save);
    void tick();

  private:
    IItemGranter &granter_;
    ApState &state_;
    ApSaveState &save_;
};

} // namespace mth
