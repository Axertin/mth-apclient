#include "mth/game_hooks.hpp"

#include <cstdint>

#include "mth/core/game_events.hpp"
#include "mth/core/game_symbols.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

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

    struct Spec
    {
        const char *label;
        const char *symbol;
        void *replacement;
        void **trampoline;
    };
    const Spec specs[kCount] = {
        {"Game::FixedUpdate", sym::game_fixed_update, reinterpret_cast<void *>(&repl_game_fixed_update), reinterpret_cast<void **>(&g_orig_game_fixed_update)},
        {"Game::Update", sym::game_update, reinterpret_cast<void *>(&repl_game_update), reinterpret_cast<void **>(&g_orig_game_update)},
        {"World::Update", sym::world_update, reinterpret_cast<void *>(&repl_world_update), reinterpret_cast<void **>(&g_orig_world_update)},
        {"ycUpdateQueue::Update", sym::update_queue, reinterpret_cast<void *>(&repl_update_queue), reinterpret_cast<void **>(&g_orig_update_queue)},
    };

    for (const auto &s : specs)
    {
        const auto addr = pal::resolve_game_symbol(s.symbol);
        if (addr == 0)
        {
            pal::logf(pal::LogLevel::Error, "GameHooks: symbol %s not found; %s not hooked", s.symbol, s.label);
            continue;
        }
        void *target = reinterpret_cast<void *>(addr);
        const auto id = pal::hook_engine().install_hook(target, s.replacement, s.trampoline);
        if (id == pal::kInvalidHookId)
        {
            pal::logf(pal::LogLevel::Error, "GameHooks: failed to hook %s at %p", s.label, target);
        }
        else
        {
            ids_[installed_++] = id;
            pal::logf(pal::LogLevel::Info, "GameHooks: hooked %s at %p via symbol (id=%llu)", s.label, target, static_cast<unsigned long long>(id));
        }
    }
}

GameHooks::~GameHooks()
{
    for (std::size_t i = 0; i < installed_; ++i)
        pal::hook_engine().remove_hook(ids_[i]);
    g_sink = nullptr;
}

} // namespace mth
