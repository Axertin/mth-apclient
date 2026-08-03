#include "mth/features/player_tracker.hpp"

#include <cmath>

#include "mod/mod_api.hpp"
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
    // Prefer the game's own live-Player pointer: it is nulled inside Player::~Player, so it goes away with
    // the object. The ctor capture has no matching teardown signal - World::Destroy is the only thing that
    // clears it, and a Player can be freed well before (or without) one, leaving a pointer that still looks
    // canonical but whose fields are recycled heap. Walking it faulted on the Coltrane rest ride (#157).
    // Null means no live player this tick; every caller re-tries, so deferring is lossless.
    void *p = mod::player_component_available() ? mod::player_component() : g_player;

    // A non-null-but-non-canonical value means a hook or the API resolved wrong. Fail closed (return null;
    // upgrades/deathlink/abilities/grants all null-check) + warn once.
    if (p != nullptr && !pal::pointer_looks_valid(p))
    {
        static bool warned = false;
        if (!warned)
        {
            warned = true;
            pal::logf(pal::LogLevel::Warn, "player pointer looks invalid (%p); write paths (upgrades/deathlink/abilities) disabled this session", p);
        }
        return nullptr;
    }
    return p;
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

void PlayerTracker::invalidate_player()
{
    // World teardown frees the Player; a cached pointer past this point is a use-after-free the next time
    // a tick writes through it (apply_upgrades then Player::UpdateStats). Position is per-player, so drop
    // it too, since it is recaptured on the next PlayerTrackable::Update.
    g_player = nullptr;
    g_have_pos = false;
}

} // namespace mth
