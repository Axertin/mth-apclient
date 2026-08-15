#pragma once

#include <functional>
#include <string>
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
    // on_session_end fires on the game thread when the link reports a different (seed, slot), immediately
    // before that connection's ApConnected is applied. Callers drop the previous session's state there; a
    // reconnect to the same seed+slot never fires it.
    // on_death carries the bounce's sending slot and cause so the death can be attributed to a player.
    ApCoordinator(IApLink &link, ApState &state, std::function<void(const std::string &source, const std::string &cause)> on_death = {},
                  std::function<void(const std::vector<BannerSegment> &)> on_broadcast = {}, std::function<void(const std::vector<ScoutInfo> &)> on_scout = {},
                  std::function<void()> on_session_reset = {}, std::function<void()> on_session_end = {});

    void tick();

  private:
    IApLink &link_;
    ApState &state_;
    std::function<void(const std::string &source, const std::string &cause)> on_death_;
    std::function<void(const std::vector<BannerSegment> &)> on_broadcast_;
    std::function<void(const std::vector<ScoutInfo> &)> on_scout_;
    std::function<void()> on_session_reset_;
    std::function<void()> on_session_end_;
};

} // namespace mth
