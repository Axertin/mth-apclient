#include "mth/core/ap/ap_coordinator.hpp"

#include <variant>

#include "mth/core/ap/ap_events.hpp"
#include "mth/core/ap/ap_link.hpp"
#include "mth/core/ap/ap_state.hpp"
#include "pal/pal_log.hpp"

namespace mth
{

ApCoordinator::ApCoordinator(IApLink &link, ApState &state, std::function<void()> on_death,
                             std::function<void(const std::vector<BannerSegment> &)> on_broadcast, std::function<void(const std::vector<ScoutInfo> &)> on_scout,
                             std::function<void()> on_session_reset)
    : link_(link), state_(state), on_death_(std::move(on_death)), on_broadcast_(std::move(on_broadcast)), on_scout_(std::move(on_scout)),
      on_session_reset_(std::move(on_session_reset))
{
}

void ApCoordinator::tick()
{
    const auto events = link_.drain_events();
    if (!events.empty())
        pal::logf(pal::LogLevel::Debug, "coordinator: applying %zu inbound event(s)", events.size());
    for (const auto &ev : events)
    {
        state_.apply(ev);
        if (std::get_if<ApDeathReceived>(&ev) && on_death_)
            on_death_();
        if (const auto *b = std::get_if<ApPrintBroadcast>(&ev); b && on_broadcast_)
            on_broadcast_(b->segments);
        if (const auto *s = std::get_if<ApScoutInfo>(&ev); s && on_scout_)
            on_scout_(s->locations);
        // Session boundary: a fresh connect or a disconnect both invalidate per-session caches keyed
        // off the prior connection (e.g. scouted shop info keyed by slot number, which is reused
        // across servers/seeds).
        if ((std::get_if<ApConnected>(&ev) || std::get_if<ApDisconnected>(&ev)) && on_session_reset_)
            on_session_reset_();
    }
}

} // namespace mth
