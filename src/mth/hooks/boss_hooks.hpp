#pragma once

#include "mth/hooks/scoped_hook.hpp"

namespace mth
{

class RandoBridge;

// Outbound boss-defeat checks via the two live death funnels (TriggerDeathSequence,
// OnDefeatedNoSkeleton). Some bosses hit both in one death; the bridge dedups.
class BossHooks
{
  public:
    explicit BossHooks(RandoBridge &bridge);
    ~BossHooks();
    BossHooks(const BossHooks &) = delete;
    BossHooks &operator=(const BossHooks &) = delete;

  private:
    ScopedHook trigger_death_;
    ScopedHook on_defeated_;
};

} // namespace mth
