#include "mth/core/stat_cap_state.hpp"

#include <algorithm>

#include "mth/core/ap_ids.hpp"
#include "mth/core/ap_state.hpp"

namespace mth
{

void StatCapState::recompute(const ApState &state)
{
    for (int s = 0; s < kStatCount; ++s)
        counts_[s] = 0;
    for (const auto &it : state.received_items())
        if (is_stat_cap_item(it.item_id))
            ++counts_[stat_cap_item_stat(it.item_id)];
}

void StatCapState::set_counts(int attack, int defense, int sidearm)
{
    counts_[0] = attack;
    counts_[1] = defense;
    counts_[2] = sidearm;
}

int StatCapState::granted(int stat) const
{
    if (stat < 0 || stat >= kStatCount)
        return 0;
    return counts_[stat];
}

int StatCapState::enforced_cap(int stat, int vanilla_cap) const
{
    if (stat < 0 || stat >= kStatCount)
        return vanilla_cap;
    return std::min(vanilla_cap, counts_[stat]);
}

} // namespace mth
