#pragma once

#include <functional>
#include <memory>
#include <set>

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
    ~InboundGranter();

    InboundGranter(const InboundGranter &) = delete;
    InboundGranter &operator=(const InboundGranter &) = delete;

    void tick();

  private:
    // A receipt the granter accepted but has not applied yet is NOT durable, so it cannot be
    // suppressed by ApSaveState; it is held here until the applied sink acks it (#175).
    [[nodiscard]] bool handled(int index) const;
    bool offer(int game_type, int index);
    void on_applied(int receipt);

    IItemGranter &granter_;
    ApState &state_;
    ApSaveState &save_;
    std::function<bool()> credit_kear_key_;
    // Latched false by the dtor. The sink lives in the longer-lived granter and may by then have been
    // replaced by a successor's, so teardown disarms its own rather than clearing whatever is installed.
    std::shared_ptr<bool> alive_;
    std::set<int> in_flight_;
    int last_handled_ = -1; // last logged skip census; -1 forces the first line
    int last_ungrantable_ = -1;
    int last_in_flight_ = -1;
    int stuck_ticks_ = 0; // consecutive ticks with an undrained queue
    bool stuck_warned_ = false;
};

} // namespace mth
