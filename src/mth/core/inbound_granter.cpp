#include "mth/core/inbound_granter.hpp"

#include "mth/core/ap/ap_ids.hpp"
#include "mth/core/ap/ap_save_state.hpp"
#include "mth/core/ap/ap_state.hpp"
#include "mth/core/item_granter_interface.hpp"
#include "pal/pal_log.hpp"

namespace mth
{

// ~10s of gameplay ticks: long enough to clear a room load or spawn window, short enough to land in
// the log while the player is still in the session that produced it.
constexpr int kStuckTickWarn = 600;

InboundGranter::InboundGranter(IItemGranter &granter, ApState &state, ApSaveState &save, std::function<bool()> credit_kear_key)
    : granter_(granter), state_(state), save_(save), credit_kear_key_(std::move(credit_kear_key)), alive_(std::make_shared<bool>(true))
{
    granter_.set_applied_sink(
        [this, alive = alive_](int receipt)
        {
            if (*alive)
                on_applied(receipt);
        });
}

InboundGranter::~InboundGranter()
{
    *alive_ = false;
}

// The one place a grant becomes durable. Reached only once the item has actually been applied, so a
// queue that dies before its drain window leaves nothing marked and the receipt is retried (#175).
void InboundGranter::on_applied(int receipt)
{
    in_flight_.erase(receipt);
    save_.mark_granted(receipt);
    save_.save();
}

bool InboundGranter::handled(int index) const
{
    return save_.is_granted(index) || in_flight_.count(index) != 0;
}

bool InboundGranter::offer(int game_type, int index)
{
    in_flight_.insert(index); // before the call: grant() may ack synchronously
    if (granter_.grant(game_type, index))
        return true;
    in_flight_.erase(index);
    return false;
}

void InboundGranter::tick()
{
    int weapon_tier[kWeaponFamilyCount] = {0}; // running per-family receipt count -> progressive tier
    int fishing_tier = 0;                      // running fishing-rod receipt count -> progressive tier
    int map_tier = 0;

    const bool vanilla_kear = state_.kear_mode() == KearMode::Vanilla;

    int n_handled = 0;     // skipped: already durable or in flight
    int n_ungrantable = 0; // skipped: this path does not grant that category

    for (const auto &it : state_.received_items())
    {
        // Vanilla kear mode (#130): a Universal Kear must raise the usable-key count. The itemType-grant
        // path can't do it (an inbound replay uses slot=-1, which aliases every kear onto bit 63), so lower
        // the spent-counter by one via the injected effect instead - once per receipt, marked like a grant.
        if (vanilla_kear && is_vanilla_kear_item(it.item_id))
        {
            if (handled(it.index))
            {
                ++n_handled;
                continue;
            }
            if (!credit_kear_key_ || !credit_kear_key_())
                break; // no live save/player yet; retry next tick (do not mark)
            // Applies inline rather than through the granter's queue, so it is durable immediately.
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
            if (handled(it.index))
            {
                ++n_handled;
                continue;
            }
            const int game_type = fishing_rod_itemtype(tier);
            if (game_type < 0) // beyond the top tier: consume so it does not retry forever
            {
                pal::logf(pal::LogLevel::Warn, "inbound_granter: fishing rod tier=%d exceeds max; ignored (index=%d)", tier, it.index);
                save_.mark_granted(it.index);
                save_.save();
                continue;
            }
            if (!offer(game_type, it.index))
                break; // not ready; retry next tick (tier is recomputed from scratch)
            pal::logf(pal::LogLevel::Info, "inbound_granter: fishing rod tier=%d -> itemType=%d (index=%d) queued", tier, game_type, it.index);
            continue;
        }

        // Progressive weapon: the Nth receipt of a family grants its tier-N itemType. Count every
        // receipt (already-granted ones too) so the tier survives reloads; grant only the new ones.
        if (is_weapon_item(it.item_id))
        {
            const int fam = weapon_family(it.item_id);
            const int tier = ++weapon_tier[fam];
            if (handled(it.index))
            {
                ++n_handled;
                continue;
            }
            const int game_type = weapon_itemtype(fam, tier);
            if (game_type < 0) // beyond the family's top tier: consume so it does not retry forever
            {
                pal::logf(pal::LogLevel::Warn, "inbound_granter: weapon family=%d tier=%d exceeds max; ignored (index=%d)", fam, tier, it.index);
                save_.mark_granted(it.index);
                save_.save();
                continue;
            }
            if (!offer(game_type, it.index))
                break; // not ready; retry next tick (tier is recomputed from scratch)
            pal::logf(pal::LogLevel::Info, "inbound_granter: weapon family=%d tier=%d -> itemType=%d (index=%d) queued", fam, tier, game_type, it.index);
            continue;
        }

        if (is_map_item(it.item_id))
        {
            const int tier = ++map_tier;

            if (handled(it.index))
            {
                ++n_handled;
                continue;
            }
            const int game_type = map_itemtype(tier);
            if (game_type < 0) // beyond the top tier: consume so it does not retry forever
            {
                pal::logf(pal::LogLevel::Warn, "inbound_granter: map tier=%d exceeds max; ignored (index=%d)", tier, it.index);
                save_.mark_granted(it.index);
                save_.save();
                continue;
            }
            if (!offer(game_type, it.index))
                break; // not ready; retry next tick (tier is recomputed from scratch)
            pal::logf(pal::LogLevel::Info, "inbound_granter: map tier=%d -> itemType=%d (index=%d) queued", tier, game_type, it.index);
            continue;
        }

        // Non-vanilla/non-weapon ids are handled elsewhere (stat-caps, kear) or unhandled; capacity
        // upgrades are vanilla ids but applied by UpgradeState (popcount bits), not itemType grants.
        if (!is_vanilla_game_item(it.item_id) || is_capacity_upgrade_item(it.item_id))
        {
            ++n_ungrantable;
            continue;
        }
        if (handled(it.index)) // already granted or still in flight: silent (runs every tick)
        {
            ++n_handled;
            continue;
        }
        const int game_type = game_item_type(it.item_id);
        if (!offer(game_type, it.index))
        {
            break; // not available now; retry next tick (do not mark)
        }
        pal::logf(pal::LogLevel::Info, "inbound_granter: queued grant index=%d id=%lld (type=%d)", it.index, static_cast<long long>(it.item_id), game_type);
    }

    // Census of what this pass declined to grant. Logged only when it moves, because the per-item
    // skips are silent and a stream that produces far fewer grants than receipts is otherwise
    // invisible in a log - which is what made #175 untraceable.
    const int n_in_flight = static_cast<int>(in_flight_.size());
    if (n_handled != last_handled_ || n_ungrantable != last_ungrantable_ || n_in_flight != last_in_flight_)
    {
        pal::logf(pal::LogLevel::Debug, "inbound_granter: %zu receipt(s): %d already handled, %d not grantable here, %d in flight",
                  state_.received_items().size(), n_handled, n_ungrantable, n_in_flight);
        last_handled_ = n_handled;
        last_ungrantable_ = n_ungrantable;
        last_in_flight_ = n_in_flight;
    }

    // A queue that is accepted but never drained is this fix's own failure mode: nothing is lost
    // (the receipts stay unmarked and retry), but nothing lands either, and the census above goes
    // quiet once it reaches steady state. Say so once per stuck episode.
    if (in_flight_.empty())
    {
        stuck_ticks_ = 0;
        stuck_warned_ = false;
    }
    else if (++stuck_ticks_ >= kStuckTickWarn && !stuck_warned_)
    {
        pal::logf(pal::LogLevel::Warn, "inbound_granter: %d grant(s) accepted but not applied after %d ticks; is the World::Update drain running?", n_in_flight,
                  stuck_ticks_);
        stuck_warned_ = true;
    }
}

} // namespace mth
