#include "mth/hooks/game_hooks.hpp"

#include "mth/core/game_events.hpp"
#include "mth/core/game_symbols.hpp"

// File-scope globals: Frida replacements have no user context; exactly one GameHooks exists.
namespace
{

mth::IGameEvents *g_sink = nullptr;

void (*g_orig_game_fixed_update)() = nullptr;
void (*g_orig_game_update)(float) = nullptr;
void (*g_orig_world_update)(void *, void *) = nullptr;
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

void repl_world_update(void *self, void *ctx)
{
    if (g_sink)
        g_sink->on_world_update_pre();
    if (g_orig_world_update)
        g_orig_world_update(self, ctx);
    if (g_sink)
        g_sink->on_world_update();
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
    world_update_ =
        ScopedHook(sym::world_update, reinterpret_cast<void *>(&repl_world_update), reinterpret_cast<void **>(&g_orig_world_update), "World::Update");
    update_queue_ =
        ScopedHook(sym::update_queue, reinterpret_cast<void *>(&repl_update_queue), reinterpret_cast<void **>(&g_orig_update_queue), "ycUpdateQueue::Update");
}

GameHooks::~GameHooks()
{
    // g_sink cleared first; the repls null-check it, so a hook firing during member
    // teardown is a safe no-op forward.
    g_sink = nullptr;
}

} // namespace mth
