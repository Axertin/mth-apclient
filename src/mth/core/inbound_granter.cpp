#include "mth/core/inbound_granter.hpp"

#include "mth/core/ap_save_state.hpp"
#include "mth/core/ap_state.hpp"
#include "mth/core/item_granter.hpp"
#include "mth/core/rando_bridge.hpp"

namespace mth
{

InboundGranter::InboundGranter(IItemGranter &granter, ApState &state, ApSaveState &save) : granter_(granter), state_(state), save_(save)
{
}

void InboundGranter::tick()
{
    for (const auto &it : state_.received_items())
    {
        if (save_.is_granted(it.index))
            continue;
        if (!granter_.grant(game_item_type(it.item_id)))
            break; // not available now; retry next tick (do not mark)
        save_.mark_granted(it.index);
        save_.save();
    }
}

} // namespace mth
