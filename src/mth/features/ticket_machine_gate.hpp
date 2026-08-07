#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mth
{

// Makes the station's train-pass donation machine non-interactible (#162). Vanilla, 10000 donated bones
// dispense the generic Train Pass - an unlock the AP item is meant to be the only source of. Used both
// when the seed does not carry the machine as a location (nothing else would refuse the grant) and,
// after the check is sent, when it does (a suppressed grant leaves the machine's own collected-bit test
// false, so it would re-dispense forever).
//
// The machine has no named mod hook and no detour, so this finds it by walking the world's scene graph
// and writes the interact component's disable byte. It stays visible and solid, it just never prompts.
//
// The walk repeats on a slow cadence rather than latching after the first success: it costs microseconds,
// and re-walking is what makes a room load, a late spawn, or a recycled world pointer self-correct. No
// game pointer is held across ticks, which is what keeps a room teardown from leaving a dangling one.
class TicketMachineGate
{
  public:
    void tick();             // game thread, World::Update pre-window; the caller decides when suppression applies
    void on_world_destroy(); // walk the next room immediately rather than up to a cadence late

  private:
    int cooldown_{0};
    bool logged_extent_{false};  // one-shot scale log: a walk that sees nothing is how this breaks
    bool warned_empty_{false};   // ... and reaching nothing at all is how it breaks silently
    bool warned_capped_{false};  // either runaway guard tripping, reported once rather than per walk
    std::uintptr_t mod_base_{0}; // game module range, resolved once: the lookup takes the loader lock
    std::size_t mod_size_{0};
    std::vector<void *> buffer_;  // reused across levels and ticks; no per-node allocation
    std::vector<void *> pending_; // entities left to descend into (explicit stack, no recursion)
};

} // namespace mth
