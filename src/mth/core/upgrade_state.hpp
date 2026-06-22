#pragma once

#include "mth/core/ap_ids.hpp"

namespace mth
{
class ApState;

// Counts received capacity-upgrade items (68..72) per type, clamped to each cap. pal::apply_upgrades
// pushes counts() to the save; dirty() gates re-applying so the owner only writes on change.
class UpgradeState
{
  public:
    void recompute(const ApState &state); // refresh clamped per-type counts from received items

    [[nodiscard]] const int *counts() const // [kUpgradeCount], index = upgrade_index()
    {
        return counts_;
    }
    [[nodiscard]] bool dirty() const // counts changed since the last mark_applied()
    {
        return dirty_;
    }
    void mark_applied(); // counts have been pushed to the game; clears dirty until they change again

  private:
    int counts_[kUpgradeCount]{};
    int applied_[kUpgradeCount]{}; // last counts pushed to the game; 0 = nothing applied (matches a fresh save)
    bool dirty_{false};
};

} // namespace mth
