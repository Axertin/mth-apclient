#include "mth/core/inbound_granter.hpp"

#include "mth/core/ap_ids.hpp"
#include "mth/core/ap_save_state.hpp"
#include "mth/core/ap_state.hpp"
#include "mth/core/item_granter.hpp"
#include "pal/pal_log.hpp"

namespace mth
{

InboundGranter::InboundGranter(IItemGranter &granter, ApState &state, ApSaveState &save) : granter_(granter), state_(state), save_(save)
{
}

void InboundGranter::tick()
{
    int weapon_tier[kWeaponFamilyCount] = {0}; // running per-family receipt count -> progressive tier

    for (const auto &it : state_.received_items())
    {
        // Progressive weapon: the Nth receipt of a family grants its tier-N itemType. Count every
        // receipt (already-granted ones too) so the tier survives reloads; grant only the new ones.
        if (is_weapon_item(it.item_id))
        {
            const int fam = weapon_family(it.item_id);
            const int tier = ++weapon_tier[fam];
            if (save_.is_granted(it.index))
                continue;
            const int game_type = weapon_itemtype(fam, tier);
            if (game_type < 0) // beyond the family's top tier: consume so it does not retry forever
            {
                pal::logf(pal::LogLevel::Warn, "inbound_granter: weapon family=%d tier=%d exceeds max; ignored (index=%d)", fam, tier, it.index);
                save_.mark_granted(it.index);
                save_.save();
                continue;
            }
            if (!granter_.grant(game_type))
                break; // not ready; retry next tick (tier is recomputed from scratch)
            save_.mark_granted(it.index);
            save_.save();
            pal::logf(pal::LogLevel::Info, "inbound_granter: weapon family=%d tier=%d -> itemType=%d (index=%d) granted", fam, tier, game_type, it.index);
            continue;
        }

        // Non-vanilla, non-weapon ids are handled elsewhere (stat-caps, kear) or unhandled; never
        // hand them to the engine as an itemType.
        if (!is_vanilla_game_item(it.item_id))
            continue;
        if (save_.is_granted(it.index)) // already granted: silent (runs every tick)
            continue;
        const int game_type = game_item_type(it.item_id);
        if (!granter_.grant(game_type))
        {
            break; // not available now; retry next tick (do not mark)
        }
        save_.mark_granted(it.index);
        save_.save();
        pal::logf(pal::LogLevel::Info, "inbound_granter: queued grant index=%d id=%lld (type=%d) and persisted", it.index, static_cast<long long>(it.item_id),
                  game_type);
    }
}

} // namespace mth
