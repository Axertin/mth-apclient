#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mth
{
class RandoBridge;
class ApState;

// twin: mth/core/goal_state.hpp (pure goal_met()).
// Polls the active SaveSlot each tick and fires the AP goal when the slot_data-configured condition is met:
// finish = the game-clear flag; generators/bosses = popcount of the matching SaveSlot bitfield >= threshold.
// Read-only (installs no detours), so both platforms share it; send_goal() is one-shot in the bridge.
class GoalTracker
{
  public:
    explicit GoalTracker(RandoBridge &bridge);

    void evaluate(const ApState &state); // game thread, per tick while authenticated

    [[nodiscard]] std::vector<std::string> status_lines() const;

  private:
    RandoBridge &bridge_;
    std::uintptr_t save_manager_{0};
    // Last poll, retained for status_lines().
    int config_{0};
    int gens_done_{0};
    int gens_needed_{99};
    std::uint64_t broken_mask_{0};
    int bosses_done_{0};
    int bosses_needed_{99};
    bool game_cleared_{false};
};

} // namespace mth
