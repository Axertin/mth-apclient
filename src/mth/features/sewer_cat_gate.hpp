#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace mth
{

// Makes the sewer-cat fetch vendor ("Panino") non-interactible while should_disable() is true (#88).
// Vanilla, he takes an order for an item still out in the world and later spawns the real pickup beside
// him, handing over an item AP never routed.
//
// He is found by walking the world's scene graph rather than by detouring his OnNPCEvent: the game does
// not expose his symbol through the mod API's lookup table and no signature is carved for it, so a
// detour reaches Windows not at all. This blocks the interact prompt only - narrower than swallowing
// every NPC event, and it does not cancel an order already recorded in the save, which his UpdateState
// still delivers.
//
// The walk repeats on a slow cadence rather than latching after the first success: he is reconstructed
// on each appearance, so one write does not carry to the next. No game pointer is held across ticks,
// which is what keeps a room teardown from leaving a dangling one.
class SewerCatGate
{
  public:
    explicit SewerCatGate(std::function<bool()> should_disable);

    void tick();             // game thread; walks nothing at all unless should_disable()
    void on_world_destroy(); // walk the next room immediately rather than up to a cadence late

  private:
    void disable_vendor(void *behavior, void *sibling_interact, bool sibling_unique);

    std::function<bool()> should_disable_;
    int cooldown_{0};
    bool logged_extent_{false};      // one-shot scale log: a walk that silently sees nothing is how this breaks
    bool warned_empty_{false};       // ... and reaching nothing at all is how it breaks silently
    bool warned_no_interact_{false}; // found him but could not attribute an interact component to him
    bool warned_capped_{false};      // either runaway guard tripping, reported once rather than per walk
    std::uintptr_t mod_base_{0};     // game module range, resolved once: the lookup takes the loader lock
    std::size_t mod_size_{0};
    std::vector<void *> buffer_;  // reused across levels and ticks; no per-node allocation
    std::vector<void *> pending_; // entities left to descend into (explicit stack, no recursion)
};

} // namespace mth
