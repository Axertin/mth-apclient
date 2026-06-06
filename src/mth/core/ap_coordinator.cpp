#include "mth/core/ap_coordinator.hpp"

#include "mth/core/ap_link.hpp"
#include "mth/core/ap_state.hpp"
#include "pal/pal_log.hpp"

namespace mth
{

ApCoordinator::ApCoordinator(IApLink &link, ApState &state) : link_(link), state_(state)
{
}

void ApCoordinator::tick()
{
    const auto events = link_.drain_events();
    if (!events.empty())
        pal::logf(pal::LogLevel::Debug, "coordinator: applying %zu inbound event(s)", events.size());
    for (const auto &ev : events)
        state_.apply(ev);
}

} // namespace mth
