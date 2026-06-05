#include "mth/core/rando_bridge.hpp"

#include "mth/core/ap_save_state.hpp"

namespace mth
{

RandoBridge::RandoBridge(IApLink &link, ApState &state) : link_(link), state_(state)
{
}

void RandoBridge::attach_save_state(ApSaveState &save)
{
    save_ = &save;
    sent_.clear(); // session fallback is superseded by the durable checked-set
}

bool RandoBridge::is_ap_location(int collection_slot) const
{
    return collection_slot >= 0 && state_.is_valid_location(ap_loc_id(collection_slot));
}

void RandoBridge::on_location_collected(int collection_slot)
{
    if (!is_ap_location(collection_slot))
        return; // not a location the server owns (loose drop, unknown, negative)

    const std::int64_t id = ap_loc_id(collection_slot);

    if (save_ != nullptr)
    {
        if (save_->is_checked(collection_slot))
            return; // already recorded; flush() handles any resend
        save_->mark_checked(collection_slot);
        save_->save();
    }
    else if (!sent_.insert(id).second)
    {
        return; // session-only dedup before a save state attaches
    }

    if (link_.is_connected())
        link_.send_locations({id});
    // else: recorded; the next flush() (on connect) resends it
}

void RandoBridge::flush()
{
    if (!link_.is_connected())
        return;

    std::vector<std::int64_t> ids;
    if (save_ != nullptr)
        for (int slot : save_->checked())
            ids.push_back(ap_loc_id(slot));
    else
        ids.assign(sent_.begin(), sent_.end());

    if (!ids.empty())
        link_.send_locations(ids);
}

} // namespace mth
