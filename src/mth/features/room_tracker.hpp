#pragma once

#include <cstdint>

namespace mth
{

// Caches the live screen identity, polled once per World::Update end from the native accessors: the
// current-room index qualified by the current gamestate. Room indices are only unique within a
// gamestate, so the two are packed together. Installs no detours. Game-thread-only state.
class RoomTracker
{
  public:
    RoomTracker() = default;
    ~RoomTracker();
    RoomTracker(const RoomTracker &) = delete;
    RoomTracker &operator=(const RoomTracker &) = delete;

    void poll(); // game thread, per World::Update end

    // Packed screen id: gamestate in the high 16 bits, room index in the low 16. false until the first
    // reading taken in a gameplay gamestate, so menus report nothing at all.
    [[nodiscard]] bool current_screen(std::uint32_t *out) const;
};

} // namespace mth
