#include "mth/hooks/room_tracker.hpp"

#include <cstdint>

#include "mth/core/game_symbols.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"

namespace
{

std::uint32_t g_room_idx = 0;
bool g_have_room = false;

void (*g_orig_room_update)(void *, void *) = nullptr;

void repl_room_update(void *self, void *ctx)
{
    if (g_orig_room_update)
        g_orig_room_update(self, ctx);

    std::uint32_t idx = 0;
    if (self != nullptr && pal::current_room_index(self, &idx))
    {
        if (!g_have_room || idx != g_room_idx)
            pal::logf(pal::LogLevel::Info, "area: room %u", idx);
        g_room_idx = idx;
        g_have_room = true;
    }
}

} // namespace

namespace mth
{

RoomTracker::RoomTracker()
{
    update_hook_ = ScopedHook(sym::room_manager_update, reinterpret_cast<void *>(&repl_room_update), reinterpret_cast<void **>(&g_orig_room_update),
                              "RoomManager::Update");
}

RoomTracker::~RoomTracker()
{
    g_room_idx = 0;
    g_have_room = false;
}

bool RoomTracker::current_room(std::uint32_t *out) const
{
    if (!g_have_room)
        return false;
    *out = g_room_idx;
    return true;
}

} // namespace mth
