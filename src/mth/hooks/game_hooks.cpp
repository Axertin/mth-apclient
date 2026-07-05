#include "mth/hooks/game_hooks.hpp"

#include "mod/mod_api.hpp"
#include "mth/core/data/game_symbols.hpp"
#include "mth/core/game_events.hpp"

// File-scope globals: Frida replacements have no user context; exactly one GameHooks exists.
namespace
{

mth::IGameEvents *g_sink = nullptr;

void (*g_orig_game_fixed_update)() = nullptr;
void (*g_orig_game_update)(float) = nullptr;
void (*g_orig_update_queue)(void *, float) = nullptr;

void repl_game_fixed_update()
{
    if (g_orig_game_fixed_update)
        g_orig_game_fixed_update();
    if (g_sink)
        g_sink->on_game_fixed_update();
}

void repl_game_update(float dt)
{
    if (g_orig_game_update)
        g_orig_game_update(dt);
    if (g_sink)
        g_sink->on_game_update(dt);
}

// World::Update is not detoured: its pre-update spawn window is delivered by the native "WorldUpdate"
// mod hook (fires at the top of World::Update, before the update queue runs), so no original to forward.
void world_update_notify()
{
    if (g_sink)
        g_sink->on_world_update_pre();
}

// World::Destroy has no original to forward: it is delivered by the native "WorldDestroy" mod hook.
void world_destroy_notify()
{
    if (g_sink)
        g_sink->on_world_destroy();
}

void repl_update_queue(void *self, float dt)
{
    if (g_orig_update_queue)
        g_orig_update_queue(self, dt);
    if (g_sink)
        g_sink->on_update_queue(dt);
}

} // namespace

namespace mth
{

GameHooks::GameHooks(IGameEvents &sink)
{
    g_sink = &sink;
    fixed_update_ = ScopedHook(sym::game_fixed_update, reinterpret_cast<void *>(&repl_game_fixed_update), reinterpret_cast<void **>(&g_orig_game_fixed_update),
                               "Game::FixedUpdate");
    update_ = ScopedHook(sym::game_update, reinterpret_cast<void *>(&repl_game_update), reinterpret_cast<void **>(&g_orig_game_update), "Game::Update");
    mod::install_world_update_hook(&world_update_notify);
    mod::install_world_destroy_hook(&world_destroy_notify);
    update_queue_ =
        ScopedHook(sym::update_queue, reinterpret_cast<void *>(&repl_update_queue), reinterpret_cast<void **>(&g_orig_update_queue), "ycUpdateQueue::Update");
}

GameHooks::~GameHooks()
{
    mod::remove_world_destroy_hook(); // stop the mod hooks before the sink goes away
    mod::remove_world_update_hook();
    // g_sink cleared first; the repls null-check it, so a hook firing during member
    // teardown is a safe no-op forward.
    g_sink = nullptr;
}

} // namespace mth
