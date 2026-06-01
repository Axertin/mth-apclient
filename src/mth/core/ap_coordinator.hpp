#pragma once

namespace mth
{

class IApLink;
class ApState;

// Folds transport events into AP state. Lives on the game thread: tick() is
// called from on_game_fixed_update. Pure — testable with a fake link.
class ApCoordinator
{
  public:
    ApCoordinator(IApLink &link, ApState &state);

    void tick(); // drain link events → apply to state

  private:
    IApLink &link_;
    ApState &state_;
};

} // namespace mth
