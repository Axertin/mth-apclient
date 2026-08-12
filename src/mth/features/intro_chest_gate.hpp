#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mth
{

// Demotes the intro weapon chest from its starter mode to the ordinary weapon-change mode. Vanilla, the
// chest offers all three default weapons and its confirm writes the SaveSlot ownership fields directly, so
// on top of the seed's precollected starting weapon it is a free extra one. Clamping ownership after the
// fact revokes the bits but leaves the player equipped with a weapon AP never routed, and the chest still
// offering it. The ordinary mode lists owned weapons only, equips the pick and grants nothing.
//
// The mode is one byte on the chest, copied into the menu when the chest is opened, so both are cleared.
// There is no named mod hook for a CheckpointChest (it is an UnderlabUpgrade, not a Chest) and no carved
// signature, so the chest is found by walking the world's scene graph and matching its RTTI type id.
//
// The walk repeats on a slow cadence rather than latching: the intro NPC re-arms the byte from its own
// state machine, and re-walking is what makes a room load or a late spawn self-correct. Nothing is walked
// unless armed, and no game pointer is held across ticks.
class IntroChestGate
{
  public:
    // Bound AP save AND at least one weapon granted; the second half is not optional, since the ordinary
    // mode has nothing to list until a grant lands.
    void set_armed(bool on);

    void tick();             // game thread; walks nothing unless armed
    void on_world_destroy(); // walk the next room immediately rather than up to a cadence late

  private:
    void demote_chest(void *chest);

    bool armed_{false}; // published by the game-thread tick, read by the game-thread drain
    int cooldown_{0};
    bool logged_demoted_{false}; // Info on the first demotion, Debug after: this is a silent write otherwise
    bool logged_extent_{false};  // one-shot scale log: a walk that sees nothing is how this breaks
    bool warned_empty_{false};   // ... and reaching nothing at all is how it breaks silently
    bool warned_capped_{false};  // either runaway guard tripping, reported once rather than per walk
    std::uintptr_t mod_base_{0}; // game module range, resolved once: the lookup takes the loader lock
    std::size_t mod_size_{0};
    std::vector<void *> buffer_;  // reused across levels and ticks; no per-node allocation
    std::vector<void *> pending_; // entities left to descend into (explicit stack, no recursion)
};

} // namespace mth
