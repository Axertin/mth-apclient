// Player state-machine reads behind the burrow water-boundary watcher (#163). The Player struct has the
// same layout on both platforms, so one TU. The burrow observers and the forced emerge stay in the
// platform files: they touch the ability-detour statics, which the platform hook install owns.

#include <cstddef>
#include <cstdint>

#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"

namespace
{

// Player state machine (#163): current / requested / pending-request. Requesting kStateDeepWaterFall runs the
// game's own on-foot deep-water fall, which chains fall -> respawn-at-shore -> pit damage on its own timer.
// kStateMax is the InitState/UpdateState jump-table bound, used only as a sanity check on the offset.
constexpr std::ptrdiff_t kPlayerStateCurOff = 0x254;
constexpr std::ptrdiff_t kPlayerStateReqOff = 0x25c;
constexpr std::ptrdiff_t kPlayerStatePendOff = 0x260;
constexpr int kStateBurrow = 0x1c; // current state while burrowing (ground or water)
constexpr int kStateDeepWaterFall = 0xf1;
constexpr int kStateMax = 0xf5;

} // namespace

namespace pal
{

bool player_is_burrowing(void *player)
{
    if (player == nullptr)
        return false;
    const int cur = *reinterpret_cast<int *>(static_cast<char *>(player) + kPlayerStateCurOff);
    return cur == kStateBurrow;
}

bool request_deep_water_fall(void *player)
{
    if (player == nullptr)
        return false;
    char *p = static_cast<char *>(player);
    const int cur = *reinterpret_cast<int *>(p + kPlayerStateCurOff);
    // Backstop for a caller that did not confirm the burrow state first: outside the dispatch table's range
    // this is not the state field on this build, so leave the player alone.
    if (cur < 0 || cur > kStateMax)
    {
        logf(LogLevel::Warn, "abilities: player state reads %d (out of range); deep-water fall not requested", cur);
        return false;
    }
    if (cur == kStateDeepWaterFall)
        return true; // already falling
    *reinterpret_cast<int *>(p + kPlayerStateReqOff) = kStateDeepWaterFall;
    *reinterpret_cast<std::uint8_t *>(p + kPlayerStatePendOff) = 1; // the game writes this as a byte
    return true;
}

} // namespace pal
