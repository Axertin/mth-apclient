#include "mth/core/area_reporter.hpp"

#include "mth/core/ap_link.hpp"

namespace mth
{

AreaReporter::AreaReporter(IApLink &link) : link_(link)
{
}

void AreaReporter::tick(bool connected, std::optional<std::uint32_t> game_state)
{
    if (!connected)
    {
        was_connected_ = false;
        last_sent_.reset();
        return;
    }
    if (!was_connected_)
    {
        was_connected_ = true;
        last_sent_.reset(); // fresh (re)connect: force the next readable state to publish
    }
    if (!game_state)
        return;
    if (last_sent_ == game_state)
        return;
    last_sent_ = game_state;
    link_.report_area(static_cast<int>(*game_state));
}

} // namespace mth
