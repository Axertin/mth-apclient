#include <cstddef>

#include "pal/pal_game.hpp"

namespace pal
{

// Field offsets for build 6a23742f; re-derive on a game update (a wrong offset yields a bad
// position, not a crash). Selector at +0x74 picks between the two base-position sources.
bool read_player_position(const void *trackable, float out[3])
{
    if (trackable == nullptr)
        return false;
    const char *t = static_cast<const char *>(trackable);
    const auto f = [t](std::ptrdiff_t o) { return *reinterpret_cast<const float *>(t + o); };
    if (*reinterpret_cast<const unsigned char *>(t + 0x74) != 0)
    {
        out[0] = f(0x68);
        out[1] = f(0x6c);
        out[2] = f(0x70);
    }
    else
    {
        out[0] = f(0x50) + f(0x5c);
        out[1] = f(0x54) + f(0x60);
        out[2] = f(0x58) + f(0x64);
    }
    return true;
}

void *pickup_base_from_onpickup(void *onpickup_this)
{
    return static_cast<char *>(onpickup_this) - 0x180; // PickupListener subobject offset, build 6a23742f
}

} // namespace pal
