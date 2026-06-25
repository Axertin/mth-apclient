#pragma once

#include "mth/ui/dev_console.hpp"
#include "mth/ui/login_window.hpp"
#include "pal/pal_overlay.hpp"

namespace mth
{

class ICommandSink;
class BannerQueue;

// Composite overlay UI: owns the dev console and login window; fans per-window visibility
// from the platform overlay's OverlayVisibility into each child.
class OverlayRoot final : public pal::IOverlayUi
{
  public:
    OverlayRoot(ICommandSink &sink, BannerQueue &banner_queue);

    OverlayRoot(const OverlayRoot &) = delete;
    OverlayRoot &operator=(const OverlayRoot &) = delete;

    void draw(const pal::OverlayVisibility &vis) override;

  private:
    DevConsole console_;
    LoginWindow login_;
};

} // namespace mth
