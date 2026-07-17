#include "mth/core/inbound_granter.hpp"

#include "mth/core/ap/ap_ids.hpp"
#include "mth/core/ap/ap_save_state.hpp"
#include "mth/core/ap/ap_state.hpp"
#include "mth/core/item_granter_interface.hpp"
#include "pal/pal_log.hpp"

namespace mth
{

InboundGranter::InboundGranter(IItemGranter &granter, ApState &state, ApSaveState &save, std::function<bool()> credit_kear_key)
    : granter_(granter), state_(state), save_(save), credit_kear_key_(std::move(credit_kear_key))
{
}

void InboundGranter::tick()
{
    int weapon_tier[kWeaponFamilyCount] = {0}; // running per-family receipt count -> progressive tier
    int fishing_tier = 0;                      // running fishing-rod receipt count -> progressive tier
    int map_tier = 0;

    const bool vanilla_kear = state_.kear_mode() == KearMode::Vanilla;

    for (const auto &it : state_.received_items())
    {
        // Vanilla kear mode (#130): a Universal Kear must raise the usable-key count. The itemType-grant
        // path can't do it (an inbound replay uses slot=-1, which aliases every kear onto bit 63), so lower
        // the spent-counter by one via the injected effect instead -- once per receipt, marked like a grant.
        if (vanilla_kear && is_vanilla_kear_item(it.item_id))
        {
            if (save_.is_granted(it.index))
                continue;
            if (!credit_kear_key_ || !credit_kear_key_())
                break; // no live save/player yet; retry next tick (do not mark)
            save_.mark_granted(it.index);
            save_.save();
            pal::logf(pal::LogLevel::Info, "inbound_granter: credited vanilla kear key (index=%d)", it.index);
            continue;
        }

        // Progressive fishing rod: the Nth receipt grants the Nth upgrade itemType (87/88/89). Count every
        // receipt (already-granted too) so the tier survives reloads; grant only the new ones. Mirrors weapons.
        if (is_fishing_rod_item(it.item_id))
        {
            const int tier = ++fishing_tier;
            if (save_.is_granted(it.index))
                continue;
            const int game_type = fishing_rod_itemtype(tier);
            if (game_type < 0) // beyond the top tier: consume so it does not retry forever
            {
                pal::logf(pal::LogLevel::Warn, "inbound_granter: fishing rod tier=%d exceeds max; ignored (index=%d)", tier, it.index);
                save_.mark_granted(it.index);
                save_.save();
                continue;
            }
            if (!granter_.grant(game_type))
                break; // not ready; retry next tick (tier is recomputed from scratch)
            save_.mark_granted(it.index);
            save_.save();
            pal::logf(pal::LogLevel::Info, "inbound_granter: fishing rod tier=%d -> itemType=%d (index=%d) granted", tier, game_type, it.index);
            continue;
        }

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

        if (is_map_item(it.item_id))
        {
            const int tier = ++map_tier;

            if (save_.is_granted(it.index))
                continue;
            const int game_type = map_itemtype(tier);
            if (game_type < 0) // beyond the top tier: consume so it does not retry forever
            {
                pal::logf(pal::LogLevel::Warn, "inbound_granter: map tier=%d exceeds max; ignored (index=%d)", tier, it.index);
                save_.mark_granted(it.index);
                save_.save();
                continue;
            }
            if (!granter_.grant(game_type))
                break; // not ready; retry next tick (tier is recomputed from scratch)
            save_.mark_granted(it.index);
            save_.save();
            pal::logf(pal::LogLevel::Info, "inbound_granter: map tier=%d -> itemType=%d (index=%d) granted", tier, game_type, it.index);
            continue;
        }

        // Non-vanilla/non-weapon ids are handled elsewhere (stat-caps, kear) or unhandled; capacity
        // upgrades are vanilla ids but applied by UpgradeState (popcount bits), not itemType grants.
        if (!is_vanilla_game_item(it.item_id) || is_capacity_upgrade_item(it.item_id))
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
