#pragma once

#include <cstdint>

namespace mth
{
class RandoBridge;

// Outbound boss-defeat checks by polling the SaveSlot+0x280 defeat bitmask instead of detouring the
// death funnels. The bit index is the BossComponent index, so bit i maps straight to the same AP
// location the funnels used. Read-only (installs no detours), so both platforms share it.
// Only live kills count. The baseline is re-taken on every real save activation, because the game
// keeps ONE working-slot buffer: loading a save refills it in place, so neither the slot pointer nor
// a live Player distinguishes "the run's save arrived" from "a boss just died". Loading a save with
// bosses already down otherwise reported every one of them as a fresh kill.
class BossTracker
{
  public:
    explicit BossTracker(RandoBridge &bridge);

    void poll();           // game thread, per tick
    void on_save_loaded(); // game thread; a save was just activated, so re-baseline off its contents

  private:
    void prime(const void *slot, const char *why, std::uint64_t mask);

    RandoBridge &bridge_;
    std::uintptr_t save_manager_{0};
    const void *prev_slot_{nullptr};
    std::uint64_t prev_mask_{0};
    bool primed_{false};
};

} // namespace mth
