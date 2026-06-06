#include "mth/core/inbound_granter.hpp"

#include "mth/core/ap_save_state.hpp"
#include "mth/core/ap_state.hpp"
#include "mth/core/item_granter.hpp"
#include "mth/core/rando_bridge.hpp"
#include "pal/pal_log.hpp"

namespace mth
{

InboundGranter::InboundGranter(IItemGranter &granter, ApState &state, ApSaveState &save) : granter_(granter), state_(state), save_(save)
{
}

void InboundGranter::tick()
{
    for (const auto &it : state_.received_items())
    {
        if (save_.is_granted(it.index)) // already granted: silent (runs every tick)
            continue;
        const int game_type = game_item_type(it.item_id);
        if (!granter_.grant(game_type))
        {
            pal::logf(pal::LogLevel::Debug, "inbound_granter: item index=%d id=%lld (type=%d) not grantable yet; retry next tick", it.index,
                      static_cast<long long>(it.item_id), game_type);
            break; // not available now; retry next tick (do not mark)
        }
        save_.mark_granted(it.index);
        save_.save();
        pal::logf(pal::LogLevel::Info, "inbound_granter: queued grant index=%d id=%lld (type=%d) and persisted", it.index,
                  static_cast<long long>(it.item_id), game_type);
    }
}

} // namespace mth
