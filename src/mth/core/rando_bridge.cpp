#include "mth/core/rando_bridge.hpp"

#include "mth/core/ap/ap_save_state.hpp"
#include "pal/pal_log.hpp"

namespace mth
{

RandoBridge::RandoBridge(IApLink &link, ApState &state) : link_(link), state_(state)
{
}

void RandoBridge::attach_save_state(ApSaveState &save)
{
    save_ = &save;
    sent_.clear(); // session fallback superseded by durable state
    pal::logf(pal::LogLevel::Info, "bridge: save state attached (%zu already-checked); session fallback cleared", save.checked().size());
}

void RandoBridge::reset_session()
{
    detach_save_state();
    goal_sent_ = false;
}

void RandoBridge::detach_save_state()
{
    if (save_ == nullptr)
        return;
    save_ = nullptr;
    sent_.clear(); // the previous session's checks must not dedup the next one's
    pal::logf(pal::LogLevel::Info, "bridge: save state detached");
}

bool RandoBridge::is_ap_location(int collection_slot) const
{
    // No per-call logging here: this is queried for every location every frame (it floods the log).
    return collection_slot >= 0 && state_.is_valid_location(ap_loc_id(collection_slot));
}

bool RandoBridge::is_checked(int collection_slot) const
{
    if (collection_slot < 0)
        return false;
    if (save_ != nullptr)
        return save_->is_checked(collection_slot);
    return sent_.count(ap_loc_id(collection_slot)) != 0;
}

const std::set<int> *RandoBridge::checked_slots() const
{
    return save_ != nullptr ? &save_->checked() : nullptr;
}

void RandoBridge::on_location_collected(int collection_slot)
{
    const std::int64_t id = ap_loc_id(collection_slot);
    if (!is_ap_location(collection_slot))
    {
        pal::logf(pal::LogLevel::Warn, "bridge: on_location_collected slot=%d id=%lld is NOT a valid AP location; not sent", collection_slot,
                  static_cast<long long>(id));
        return;
    }

    if (save_ != nullptr)
    {
        if (save_->is_checked(collection_slot))
        {
            pal::logf(pal::LogLevel::Debug, "bridge: slot=%d id=%lld already checked; not resending", collection_slot, static_cast<long long>(id));
            return;
        }
        save_->mark_checked(collection_slot);
        save_->save();
    }
    else if (!sent_.insert(id).second)
    {
        return; // session-only dedup
    }

    const bool connected = link_.is_connected();
    pal::logf(pal::LogLevel::Info, "bridge: location slot=%d id=%lld checked+persisted; %s", collection_slot, static_cast<long long>(id),
              connected ? "sending to server" : "queued (offline, will flush on connect)");
    if (connected)
        link_.send_locations({id});
}

bool RandoBridge::reconcile_server_checked(int collection_slot)
{
    if (save_ == nullptr)
        return false; // App reconciles only once inbound is ready; ids stay pending in ApState until then
    if (!is_ap_location(collection_slot))
        return false;
    if (save_->is_checked(collection_slot))
        return false;
    save_->mark_checked(collection_slot); // no send; caller batches the save()
    pal::logf(pal::LogLevel::Info, "bridge: server-checked slot=%d (Collect/coop); marked locally, not resent", collection_slot);
    return true;
}

void RandoBridge::flush()
{
    if (!link_.is_connected())
    {
        pal::logf(pal::LogLevel::Debug, "bridge: flush skipped (not connected)");
        return;
    }

    std::vector<std::int64_t> ids;
    if (save_ != nullptr)
        for (int slot : save_->checked())
            ids.push_back(ap_loc_id(slot));
    else
        ids.assign(sent_.begin(), sent_.end());

    pal::logf(pal::LogLevel::Info, "bridge: flush resending %zu checked location id(s)", ids.size());
    if (!ids.empty())
        link_.send_locations(ids);
}

void RandoBridge::request_scouts(const std::vector<int> &collection_slots)
{
    std::vector<std::int64_t> ids;
    for (int slot : collection_slots)
        if (is_ap_location(slot))
            ids.push_back(ap_loc_id(slot));
    if (!ids.empty())
        link_.scout_locations(ids);
}

void RandoBridge::send_goal()
{
    if (goal_sent_)
        return;
    if (!state_.authenticated())
        return; // not an AP session; don't latch, so a later connected defeat can still send
    goal_sent_ = true;
    link_.set_goal();
    pal::logf(pal::LogLevel::Info, "goal: condition met; AP goal sent");
}

} // namespace mth
