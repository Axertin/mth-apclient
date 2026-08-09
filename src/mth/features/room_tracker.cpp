#include "mth/features/room_tracker.hpp"

#include <cstdint>

#include "mod/mod_api.hpp"
#include "mth/core/data/game_state_ids.hpp"
#include "pal/pal_log.hpp"

namespace
{

std::uint32_t g_screen = 0;
bool g_have_screen = false;

} // namespace

namespace mth
{

void RoomTracker::poll()
{
    // Menu worlds (title, profile select, the in-run overlays) tick World::Update too, and GetRoomIndex
    // keeps returning the last room throughout, so the gamestate is what separates "in a room" from
    // "in a menu". Reporting nothing outside a room is the contract AreaReporter is built on: it
    // early-returns on an absent screen without clearing what it last sent, so a menu neither
    // publishes a bogus screen nor re-publishes the real one on the way back.
    const int gs = mod::current_game_state();
    if (!is_gameplay_game_state(gs))
        return;

    // RoomManager::BeginRoomTransition recomputes the room index by walking the camera's bound list and
    // stores 0 when nothing matches - and 0 is also a legitimate room - so a mid-transition reading
    // cannot be told from a real one by value. That same call zeroes the room timer BEFORE recomputing
    // the index, and every transition state leaves the timer frozen, so a positive timer is the game's
    // own "this room is settled" signal. Fails open if the build lacks the entry: reporting a transient
    // beats reporting nothing. Known hole: Player::UpdateResurrectur rewrites the index without zeroing
    // the timer, so a failed lookup on respawn would still get through.
    if (mod::room_time_api_available() && !(mod::room_time() > 0.0f))
        return;

    const int room = mod::room_index();
    if (room < 0)
        return;

    const std::uint32_t screen = (static_cast<std::uint32_t>(gs) << 16) | (static_cast<std::uint32_t>(room) & 0xFFFFu);
    if (g_have_screen && screen == g_screen)
        return;
    g_screen = screen;
    g_have_screen = true;
    pal::logf(pal::LogLevel::Info, "area: gamestate %d room %d -> screen 0x%x", gs, room, screen);
}

RoomTracker::~RoomTracker()
{
    g_screen = 0;
    g_have_screen = false;
}

bool RoomTracker::current_screen(std::uint32_t *out) const
{
    if (!g_have_screen)
        return false;
    *out = g_screen;
    return true;
}

} // namespace mth
