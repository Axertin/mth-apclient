#pragma once

namespace mth
{

class BannerQueue;

// Renders the active BannerQueue message in the upper-right corner via the ImGui foreground draw
// list (no window -> never captures input). Drawn every frame, independent of the dev-console toggle.
class BroadcastBanner
{
  public:
    explicit BroadcastBanner(BannerQueue &queue) : queue_(queue)
    {
    }

    void draw(); // call between ImGui::NewFrame() and Render()

  private:
    BannerQueue &queue_;
};

} // namespace mth
