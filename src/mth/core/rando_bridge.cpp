#include "mth/core/rando_bridge.hpp"

namespace mth
{

RandoBridge::RandoBridge(IApLink &link, ApState &state) : link_(link), state_(state)
{
}

void RandoBridge::on_location_collected(int collection_slot)
{
    if (collection_slot < 0)
        return; // non-location pickup (enemy drop, etc.)

    const std::int64_t id = ap_loc_id(collection_slot);
    if (!state_.is_valid_location(id))
        return; // server does not know this location for our slot
    if (!sent_.insert(id).second)
        return; // already reported

    link_.send_locations({id});
}

} // namespace mth
