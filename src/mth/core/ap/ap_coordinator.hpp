#pragma once

#include <functional>
#include <vector>

namespace mth
{

class IApLink;
class ApState;
struct BannerSegment;
struct ScoutInfo;

// Folds transport events into AP state on the game thread. Testable with a fake link.
class ApCoordinator
{
  public:
    // on_session_reset fires on the game thread (from tick()) when a fresh ApConnected or ApDisconnected
    // event is drained: a session boundary, so callers reset any per-session state keyed off the prior
    // connection (e.g. clearing a scout cache) without racing a render-thread caller of the same state.
    ApCoordinator(IApLink &link, ApState &state, std::function<void()> on_death = {}, std::function<void(const std::vector<BannerSegment> &)> on_broadcast = {},
                  std::function<void(const std::vector<ScoutInfo> &)> on_scout = {}, std::function<void()> on_session_reset = {});

    void tick();

  private:
    IApLink &link_;
    ApState &state_;
    std::function<void()> on_death_;
    std::function<void(const std::vector<BannerSegment> &)> on_broadcast_;
    std::function<void(const std::vector<ScoutInfo> &)> on_scout_;
    std::function<void()> on_session_reset_;
};

} // namespace mth
