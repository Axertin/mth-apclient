#include "mth/core/ap_coordinator.hpp"

#include "mth/core/ap_link.hpp"
#include "mth/core/ap_state.hpp"

namespace mth
{

ApCoordinator::ApCoordinator(IApLink &link, ApState &state) : link_(link), state_(state)
{
}

void ApCoordinator::tick()
{
    for (const auto &ev : link_.drain_events())
        state_.apply(ev);
}

} // namespace mth
