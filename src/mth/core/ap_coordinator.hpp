#pragma once

namespace mth
{

class IApLink;
class ApState;

// Folds transport events into AP state on the game thread. Testable with a fake link.
class ApCoordinator
{
  public:
    ApCoordinator(IApLink &link, ApState &state);

    void tick();

  private:
    IApLink &link_;
    ApState &state_;
};

} // namespace mth
