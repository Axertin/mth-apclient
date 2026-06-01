#include "mth/game_hooks.hpp"

#include <cstdint>

#include "mth/core/game_events.hpp"
#include "mth/core/offsets.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

// Detour replacements + saved originals. File-scope globals because Frida calls
// the replacements with no user context; there is exactly one GameHooks (owned
// by App). Each replacement forwards to the original first (so handlers observe
// post-update state), then fires the semantic event. Signatures mirror the
// engine functions exactly (x86_64: `this` is just the first integer arg).
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

GameHooks::GameHooks(Build build, IGameEvents &sink)
{
    const auto &off = offsets_for(build);
    if (off.game_fixed_update == 0)
    {
        pal::logf(pal::LogLevel::Warn, "GameHooks: no offset table for this build; tick hooks not installed");
        return;
    }

    g_sink = &sink;
    const auto base = pal::game_module().base;

    struct Spec
    {
        const char *name;
        std::uintptr_t off;
        void *replacement;
        void **trampoline;
    };
    const Spec specs[kCount] = {
        {"Game::FixedUpdate", off.game_fixed_update, reinterpret_cast<void *>(&repl_game_fixed_update), reinterpret_cast<void **>(&g_orig_game_fixed_update)},
        {"Game::Update", off.game_update, reinterpret_cast<void *>(&repl_game_update), reinterpret_cast<void **>(&g_orig_game_update)},
        {"World::Update", off.world_update, reinterpret_cast<void *>(&repl_world_update), reinterpret_cast<void **>(&g_orig_world_update)},
        {"ycUpdateQueue::Update", off.update_queue, reinterpret_cast<void *>(&repl_update_queue), reinterpret_cast<void **>(&g_orig_update_queue)},
    };

    for (const auto &s : specs)
    {
        void *target = reinterpret_cast<void *>(base + s.off);
        const auto id = pal::hook_engine().install_hook(target, s.replacement, s.trampoline);
        if (id == pal::kInvalidHookId)
        {
            pal::logf(pal::LogLevel::Error, "GameHooks: failed to hook %s at %p", s.name, target);
        }
        else
        {
            ids_[installed_++] = id;
            pal::logf(pal::LogLevel::Info, "GameHooks: hooked %s at %p (id=%llu)", s.name, target, static_cast<unsigned long long>(id));
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
