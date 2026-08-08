#pragma once

namespace mth
{

// Serves the live Player* and a position cache sampled once per tick. Position comes from the native
// mod API's PlayerGetPos3, which reports failure rather than faulting in the pre-World::Update spawn
// window, so the sample no longer has to run in-context inside PlayerTrackable::Update. The Player*
// comes from the game's own live-player pointer where the build exposes it, falling back to whatever
// a game-thread hook last handed to note_player. All state game-thread-only.
class PlayerTracker
{
  public:
    PlayerTracker() = default;
    ~PlayerTracker();
    PlayerTracker(const PlayerTracker &) = delete;
    PlayerTracker &operator=(const PlayerTracker &) = delete;

    [[nodiscard]] void *player() const;
    [[nodiscard]] bool position(float out[3]) const; // false until the first successful sample
    void sample_position();                          // per-tick; the only writer of the position cache
    void note_player(void *player);                  // refresh the fallback capture from another game-thread hook
    void invalidate_player();                        // drop the fallback capture on world teardown (its object is about to be freed)
};

} // namespace mth
