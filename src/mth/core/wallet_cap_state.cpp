#include "mth/core/wallet_cap_state.hpp"

#include "mth/core/ap/ap_ids.hpp"
#include "mth/core/ap/ap_state.hpp"

namespace mth
{

void WalletCapState::recompute(const ApState &state)
{
    count_ = 0;
    for (const auto &it : state.received_items())
        if (is_wallet_item(it.item_id))
            ++count_;
}

void WalletCapState::set_count(int count)
{
    count_ = count;
}

std::optional<int> WalletCapState::enforced_cap() const
{
    return wallet_cap_for(count_);
}

} // namespace mth
