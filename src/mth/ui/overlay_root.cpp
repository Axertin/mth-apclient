#include "mth/ui/overlay_root.hpp"

namespace mth
{

OverlayRoot::OverlayRoot(ICommandSink &sink, BannerQueue &banner_queue) : console_(sink, banner_queue), login_(sink)
{
}

void OverlayRoot::draw(const pal::OverlayVisibility &vis)
{
    console_.draw(vis.console_open);
    login_.draw(vis.login_open);
}

} // namespace mth
