#include "mth/core/upgrade_state.hpp"

#include <algorithm>

#include "mth/core/ap_state.hpp"

namespace mth
{

void UpgradeState::recompute(const ApState &state)
{
    int next[kUpgradeCount]{};
    for (const auto &it : state.received_items())
        if (is_capacity_upgrade_item(it.item_id))
            ++next[upgrade_index(it.item_id)];
    for (int i = 0; i < kUpgradeCount; ++i)
        next[i] = std::min(next[i], kUpgradeCaps[i]);

    dirty_ = !std::equal(next, next + kUpgradeCount, applied_);
    std::copy(next, next + kUpgradeCount, counts_);
}

void UpgradeState::mark_applied()
{
    std::copy(counts_, counts_ + kUpgradeCount, applied_);
    dirty_ = false;
}

} // namespace mth
