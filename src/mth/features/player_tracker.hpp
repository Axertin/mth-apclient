#pragma once

#include "mth/hooks/scoped_hook.hpp"

namespace mth
{

// Tracks the live Player* (Player ctor hook, refreshable by other hooks) and a position
// cache captured inside PlayerTrackable::Update — in-context, because reading the
// position from the pre-World::Update spawn window walks an invalid camera graph and
// faults. All state game-thread-only.
class PlayerTracker
{
  public:
    PlayerTracker();
    ~PlayerTracker();
    PlayerTracker(const PlayerTracker &) = delete;
    PlayerTracker &operator=(const PlayerTracker &) = delete;

    [[nodiscard]] void *player() const;
    [[nodiscard]] bool position(float out[3]) const; // false until the first in-context capture
    void note_player(void *player);                  // refresh from another game-thread hook

  private:
    ScopedHook ctor_hook_;
    ScopedHook trackable_update_;
};

} // namespace mth
