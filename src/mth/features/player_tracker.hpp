#pragma once

#include "mth/hooks/scoped_hook.hpp"

namespace mth
{

// Serves the live Player* and a position cache captured inside PlayerTrackable::Update,
// in-context, because reading the position from the pre-World::Update spawn window walks
// an invalid camera graph and faults. The Player* comes from the game's own live-player
// pointer where the build exposes it, falling back to the ctor hook's capture (refreshable
// by other hooks) otherwise. All state game-thread-only.
class PlayerTracker
{
  public:
    PlayerTracker();
    ~PlayerTracker();
    PlayerTracker(const PlayerTracker &) = delete;
    PlayerTracker &operator=(const PlayerTracker &) = delete;

    [[nodiscard]] void *player() const;
    [[nodiscard]] bool position(float out[3]) const; // false until the first in-context capture
    void note_player(void *player);                  // refresh the fallback capture from another game-thread hook
    void invalidate_player();                        // drop the fallback capture on world teardown (its object is about to be freed)

  private:
    ScopedHook ctor_hook_;
    ScopedHook trackable_update_;
};

} // namespace mth
