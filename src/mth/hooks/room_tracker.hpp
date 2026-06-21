#pragma once

#include <cstdint>

#include "mth/hooks/scoped_hook.hpp"

namespace mth
{

// Caches the live current-room index, read off the RoomManager instance inside a RoomManager::Update
// hook. Per-screen: the field flips on doorway entry. Game-thread-only state.
class RoomTracker
{
  public:
    RoomTracker();
    ~RoomTracker();
    RoomTracker(const RoomTracker &) = delete;
    RoomTracker &operator=(const RoomTracker &) = delete;

    [[nodiscard]] bool current_room(std::uint32_t *out) const; // false until the first valid read

  private:
    ScopedHook update_hook_;
};

} // namespace mth
