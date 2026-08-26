#pragma once

#include <functional>
#include <memory>
#include <set>

#include "mth/core/trap_state.hpp" // TrapArm

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
    // arm_trap: forces a cosmetic modifier on for a while (injected by App). Empty means this build
    // cannot run traps, which consumes the receipt rather than retrying it forever.
    InboundGranter(IItemGranter &granter, ApState &state, ApSaveState &save, std::function<bool()> credit_kear_key = {},
                   std::function<TrapArm(int)> arm_trap = {});
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
    std::function<TrapArm(int)> arm_trap_;
    // Latched false by the dtor. The sink lives in the longer-lived granter and may by then have been
    // replaced by a successor's, so teardown disarms its own rather than clearing whatever is installed.
    std::shared_ptr<bool> alive_;
    std::set<int> in_flight_;
    int last_handled_ = -1; // last logged skip census; -1 forces the first line
    int last_ungrantable_ = -1;
    int last_in_flight_ = -1;
    int stuck_ticks_ = 0; // consecutive ticks with an undrained queue
    bool stuck_warned_ = false;
    // A trap's not-ready is not tied to the granter's own queue (see stuck_ticks_ above), so it needs its
    // own stall latch: consecutive ticks a trap has failed to arm, and whether that stall has been warned.
    int trap_notready_ticks_ = 0;
    bool trap_notready_warned_ = false;
};

} // namespace mth
