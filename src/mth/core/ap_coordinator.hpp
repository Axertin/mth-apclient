#pragma once

#include <functional>

namespace mth
{

class IApLink;
class ApState;

// Folds transport events into AP state on the game thread. Testable with a fake link.
class ApCoordinator
{
  public:
    ApCoordinator(IApLink &link, ApState &state, std::function<void()> on_death = {});

    void tick();

  private:
    IApLink &link_;
    ApState &state_;
    std::function<void()> on_death_;
};

} // namespace mth
