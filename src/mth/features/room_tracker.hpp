#pragma once

#include <cstdint>

#include "mth/hooks/scoped_hook.hpp"

namespace mth
{

// Caches the live screen identity: the current-room index (read off RoomManager inside a
// RoomManager::Update hook, flips on doorway entry) qualified by the current area index (captured from
// AreaManager::NewArea). Room indices are only unique within an area, so the two are packed together.
// Game-thread-only state.
class RoomTracker
{
  public:
    RoomTracker();
    ~RoomTracker();
    RoomTracker(const RoomTracker &) = delete;
    RoomTracker &operator=(const RoomTracker &) = delete;

    // Packed screen id: area index in the high 16 bits, room index in the low 16. false until the first
    // valid room read (area defaults to 0 until the first AreaManager::NewArea, then self-corrects).
    [[nodiscard]] bool current_screen(std::uint32_t *out) const;

  private:
    ScopedHook update_hook_;
    ScopedHook area_hook_;
};

} // namespace mth
