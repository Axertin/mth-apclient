#include "mth/features/player_tracker.hpp"

#include <cmath>

#include "mth/core/data/game_symbols.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_mem.hpp"

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
    // The Player ctor hook captures a real `self`; a non-null-but-non-canonical value means the hook
    // resolved wrong. Fail closed (return null; upgrades/deathlink/abilities all null-check) + warn once.
    if (g_player != nullptr && !pal::pointer_looks_valid(g_player))
    {
        static bool warned = false;
        if (!warned)
        {
            warned = true;
            pal::logf(pal::LogLevel::Warn, "player pointer looks invalid (%p); write paths (upgrades/deathlink/abilities) disabled this session", g_player);
        }
        return nullptr;
    }
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
