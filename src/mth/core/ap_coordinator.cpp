#include "mth/core/ap_coordinator.hpp"

#include <variant>

#include "mth/core/ap_events.hpp"
#include "mth/core/ap_link.hpp"
#include "mth/core/ap_state.hpp"
#include "pal/pal_log.hpp"

namespace mth
{

ApCoordinator::ApCoordinator(IApLink &link, ApState &state, std::function<void()> on_death) : link_(link), state_(state), on_death_(std::move(on_death))
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
    }
}

} // namespace mth
