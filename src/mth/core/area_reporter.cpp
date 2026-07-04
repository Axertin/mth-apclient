#include "mth/core/area_reporter.hpp"

#include "mth/core/ap/ap_link.hpp"

namespace mth
{

AreaReporter::AreaReporter(IApLink &link) : link_(link)
{
}

void AreaReporter::tick(bool connected, std::optional<std::uint32_t> screen_id)
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
    if (!screen_id)
        return;
    if (last_sent_ == screen_id)
        return;
    last_sent_ = screen_id;
    link_.report_area(static_cast<int>(*screen_id));
}

} // namespace mth
