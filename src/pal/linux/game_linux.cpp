#include "pal/pal_game.hpp"

#include "mth/core/game_symbols.hpp"
#include "pal/pal_module.hpp"

namespace
{
struct Vec3
{
    float x, y, z;
};
Vec3 (*g_get_pos)(const void *) = nullptr; // Itanium returns the vec in registers
} // namespace

namespace pal
{

bool read_player_position(const void *trackable, float out[3])
{
    if (g_get_pos == nullptr)
        g_get_pos = reinterpret_cast<Vec3 (*)(const void *)>(resolve_game_symbol(mth::sym::player_trackable_get_pos));
    if (g_get_pos == nullptr || trackable == nullptr)
        return false;
    const Vec3 v = g_get_pos(trackable);
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
    return true;
}

void *pickup_base_from_onpickup(void *onpickup_this)
{
    return onpickup_this; // Itanium routes the secondary-base adjust through a separate _ZThn thunk
}

} // namespace pal
