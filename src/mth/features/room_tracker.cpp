#include "mth/features/room_tracker.hpp"

#include <cstdint>

#include "mth/core/game_symbols.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"

namespace
{

std::uint32_t g_room_idx = 0;
bool g_have_room = false;
std::uint32_t g_area_idx = 0; // dense area index from AreaManager::NewArea; 0 until the first transition

void (*g_orig_room_update)(void *, void *) = nullptr;

void repl_room_update(void *self, void *ctx)
{
    if (g_orig_room_update)
        g_orig_room_update(self, ctx);

    std::uint32_t idx = 0;
    if (self != nullptr && pal::current_room_index(self, &idx))
    {
        if (!g_have_room || idx != g_room_idx)
            pal::logf(pal::LogLevel::Info, "area: area %u room %u", g_area_idx, idx);
        g_room_idx = idx;
        g_have_room = true;
    }
}

void (*g_orig_new_area)(void *, int, int) = nullptr;

void repl_new_area(void *self, int prev, int new_idx)
{
    if (g_orig_new_area)
        g_orig_new_area(self, prev, new_idx);

    if (new_idx >= 0)
        g_area_idx = static_cast<std::uint32_t>(new_idx);
}

} // namespace

namespace mth
{

RoomTracker::RoomTracker()
{
    update_hook_ = ScopedHook(sym::room_manager_update, reinterpret_cast<void *>(&repl_room_update), reinterpret_cast<void **>(&g_orig_room_update),
                              "RoomManager::Update");
    area_hook_ = ScopedHook(sym::area_new_area, reinterpret_cast<void *>(&repl_new_area), reinterpret_cast<void **>(&g_orig_new_area), "AreaManager::NewArea");
}

RoomTracker::~RoomTracker()
{
    g_room_idx = 0;
    g_have_room = false;
    g_area_idx = 0;
}

bool RoomTracker::current_screen(std::uint32_t *out) const
{
    if (!g_have_room)
        return false;
    *out = (g_area_idx << 16) | (g_room_idx & 0xFFFFu);
    return true;
}

} // namespace mth
