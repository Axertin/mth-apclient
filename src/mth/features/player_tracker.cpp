#include "mth/features/player_tracker.hpp"

#include <cmath>

#include "mth/core/game_symbols.hpp"
#include "pal/pal_game.hpp"

namespace
{

void *g_player = nullptr;
float g_last_pos[3]{};
bool g_have_pos = false;

void (*g_orig_player_ctor)(void *, void *, void *, void *) = nullptr;
void (*g_orig_trackable_update)(void *, void *) = nullptr;

void repl_player_ctor(void *self, void *entity, void *desc, void *setup)
{
    if (g_orig_player_ctor)
        g_orig_player_ctor(self, entity, desc, setup);
    g_player = self; // available before any pickup
}

void repl_trackable_update(void *self, void *ctx)
{
    if (g_orig_trackable_update)
        g_orig_trackable_update(self, ctx);

    float p[3];
    if (pal::read_player_position(self, p) && std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]))
    {
        g_last_pos[0] = p[0];
        g_last_pos[1] = p[1];
        g_last_pos[2] = p[2];
        g_have_pos = true;
    }
}

} // namespace

namespace mth
{

PlayerTracker::PlayerTracker()
{
    ctor_hook_ = ScopedHook(sym::player_ctor, reinterpret_cast<void *>(&repl_player_ctor), reinterpret_cast<void **>(&g_orig_player_ctor), "Player::Player");
    trackable_update_ = ScopedHook(sym::player_trackable_update, reinterpret_cast<void *>(&repl_trackable_update),
                                   reinterpret_cast<void **>(&g_orig_trackable_update), "PlayerTrackable::Update");
}

PlayerTracker::~PlayerTracker()
{
    g_player = nullptr;
    g_have_pos = false;
}

void *PlayerTracker::player() const
{
    return g_player;
}

bool PlayerTracker::position(float out[3]) const
{
    if (!g_have_pos)
        return false;
    out[0] = g_last_pos[0];
    out[1] = g_last_pos[1];
    out[2] = g_last_pos[2];
    return true;
}

void PlayerTracker::note_player(void *player)
{
    if (player != nullptr)
        g_player = player;
}

} // namespace mth
