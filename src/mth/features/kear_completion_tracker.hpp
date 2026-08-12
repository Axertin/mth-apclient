#pragma once

#include <cstdint>

namespace mth
{
class ApState;

// twin: mth/core/kear_completion.hpp (pure have_all_kears()).
// Latches the KeyMiser trade bit once the run holds every kear its mode defines, so the game's own
// KeyReward path spawns the location-150 pickup and the normal pickup hook reports the check (#174).
// Installs no detours and writes one durable save bit, so both platforms share it unchanged.
class KearCompletionTracker
{
  public:
    KearCompletionTracker();

    void evaluate(const ApState &state); // game thread, per tick while authenticated

  private:
    std::uintptr_t save_manager_{0};
};

} // namespace mth
