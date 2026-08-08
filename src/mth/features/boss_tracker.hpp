#pragma once

#include <cstdint>

namespace mth
{
class RandoBridge;

// Outbound boss-defeat checks by polling the SaveSlot+0x280 defeat bitmask instead of detouring the
// death funnels. The bit index is the BossComponent index, so bit i maps straight to the same AP
// location the funnels used. Read-only (installs no detours), so both platforms share it.
// Only live kills count: the baseline is primed silently for each SaveSlot the mod sees, so bosses
// already defeated when that slot came up are never sent. The baseline is keyed on the slot POINTER
// because the title screen binds a different slot than the run does, and a live Player is no proxy
// for "the run's save is up" - the title screen builds a full World with one.
class BossTracker
{
  public:
    explicit BossTracker(RandoBridge &bridge);

    void poll(); // game thread, per tick

  private:
    void prime(const void *slot, const char *why, std::uint64_t mask);

    RandoBridge &bridge_;
    std::uintptr_t save_manager_{0};
    const void *prev_slot_{nullptr};
    std::uint64_t prev_mask_{0};
    bool primed_{false};
};

} // namespace mth
