#pragma once

#include <functional>
#include <vector>

namespace mth
{

class IApLink;
class ApState;
struct BannerSegment;

// Folds transport events into AP state on the game thread. Testable with a fake link.
class ApCoordinator
{
  public:
    ApCoordinator(IApLink &link, ApState &state, std::function<void()> on_death = {},
                  std::function<void(const std::vector<BannerSegment> &)> on_broadcast = {});

    void tick();

  private:
    IApLink &link_;
    ApState &state_;
    std::function<void()> on_death_;
    std::function<void(const std::vector<BannerSegment> &)> on_broadcast_;
};

} // namespace mth
