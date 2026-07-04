#include "mth/features/death_hooks.hpp"

#include <utility>

#include "mth/core/game_layout.hpp"
#include "mth/core/game_symbols.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

namespace
{

std::function<void()> g_on_local_death;
std::function<void *()> g_get_player;
bool g_suppress_next_death = false; // edge-latched: set on apply, consumed in the InitDeath detect

void (*g_orig_init_death)(void *, bool) = nullptr;
bool (*g_trigger_death)(void *) = nullptr; // resolved for APPLY; not hooked

[[nodiscard]] bool is_dying(void *player)
{
    return player != nullptr && *reinterpret_cast<unsigned char *>(static_cast<char *>(player) + mth::layout::kPlayerDeathGuardOff) != 0;
}

void repl_init_death(void *self, bool b)
{
    // Death EDGE: guard byte is 0 before a fresh death; the original sets it to 1.
    const bool fresh = self != nullptr && !is_dying(self);

    if (g_orig_init_death)
        g_orig_init_death(self, b);

    if (!fresh)
        return; // re-entrant / already dying

    if (g_suppress_next_death)
    {
        g_suppress_next_death = false; // this death is one WE applied; consume, do not re-broadcast
        pal::logf(pal::LogLevel::Debug, "deathlink: applied death observed (suppressed broadcast)");
        return;
    }

    if (g_on_local_death)
    {
        pal::logf(pal::LogLevel::Info, "deathlink: local death (InitDeath edge) -> broadcasting");
        g_on_local_death();
    }
}

} // namespace

namespace mth
{

DeathHooks::DeathHooks(std::function<void()> on_local_death, std::function<void *()> get_player)
{
    g_on_local_death = std::move(on_local_death);
    g_get_player = std::move(get_player);

    g_trigger_death = reinterpret_cast<bool (*)(void *)>(pal::resolve_game_symbol(sym::player_trigger_death));
    if (g_trigger_death == nullptr)
        pal::logf(pal::LogLevel::Warn, "DeathHooks: Player::TriggerDeath not resolved; inbound deathlink apply disabled");

    init_death_ =
        ScopedHook(sym::player_init_death, reinterpret_cast<void *>(&repl_init_death), reinterpret_cast<void **>(&g_orig_init_death), "Player::InitDeath");
    if (!init_death_.installed())
        pal::logf(pal::LogLevel::Warn, "DeathHooks: deathlink detect disabled");
}

DeathHooks::~DeathHooks()
{
    g_on_local_death = nullptr;
    g_get_player = nullptr;
    g_trigger_death = nullptr;
    g_suppress_next_death = false;
}

bool DeathHooks::ready() const
{
    return init_death_.installed() && g_trigger_death != nullptr && g_get_player && g_get_player() != nullptr;
}

void DeathHooks::kill()
{
    void *p = g_get_player ? g_get_player() : nullptr;
    if (!init_death_.installed() || g_trigger_death == nullptr || p == nullptr)
    {
        pal::logf(pal::LogLevel::Warn, "deathlink: inbound death not applied (player not captured yet)");
        return;
    }
    if (is_dying(p))
    {
        pal::logf(pal::LogLevel::Debug, "deathlink: inbound death ignored (already dying)");
        return; // applying would no-op and leave a stale suppress latch
    }
    g_suppress_next_death = true; // the resulting InitDeath edge must not re-broadcast
    pal::logf(pal::LogLevel::Info, "deathlink: applying inbound death");
    g_trigger_death(p);
}

} // namespace mth
