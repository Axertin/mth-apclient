#include "mth/features/death_hooks.hpp"

#include <utility>

#include "mod/mod_api.hpp"
#include "mth/core/data/game_layout.hpp"
#include "pal/pal_log.hpp"

namespace
{

// Player+0x1380: the once-per-death guard byte the game sets when a death begins (0 = alive/fresh). Reading
// it is the authoritative "is dying" signal the deathlink edge keys on. Offset, not a function sig.
[[nodiscard]] bool is_dying(void *player)
{
    return player != nullptr && *reinterpret_cast<unsigned char *>(static_cast<char *>(player) + mth::layout::kPlayerDeathGuardOff) != 0;
}

} // namespace

namespace mth
{

DeathHooks::DeathHooks(std::function<void()> on_local_death, std::function<void *()> get_player)
    : on_local_death_(std::move(on_local_death)), get_player_(std::move(get_player))
{
}

DeathHooks::~DeathHooks() = default;

void DeathHooks::poll()
{
    void *p = get_player_ ? get_player_() : nullptr;
    if (p == nullptr)
        return; // no player -> no signal; keep the edge state frozen across world transitions
    const bool dying = is_dying(p);
    // Re-arm only on a true respawn: the guard byte pulses through the death sequence, so use health > 0 as
    // the stable "alive" signal. Fall back to the guard byte if the health API is unavailable.
    const bool alive = mod::health_api_available() ? (mod::player_health() > 0.0f) : !dying;
    if (gate_.observe(dying, alive) && on_local_death_)
    {
        pal::logf(pal::LogLevel::Info, "deathlink: local death -> broadcasting");
        on_local_death_();
    }
}

void DeathHooks::kill()
{
    void *p = get_player_ ? get_player_() : nullptr;
    if (p == nullptr)
    {
        pal::logf(pal::LogLevel::Warn, "deathlink: inbound death not applied (player not captured yet)");
        return;
    }
    if (is_dying(p))
    {
        pal::logf(pal::LogLevel::Debug, "deathlink: inbound death ignored (already dying)");
        return; // applying would no-op and leave a stale suppress latch
    }
    if (!mod::player_die())
    {
        pal::logf(pal::LogLevel::Warn, "deathlink: inbound death not applied (PlayerDie API unavailable)");
        return;
    }
    gate_.arm_suppress(); // the resulting death edge (seen by a later poll) must not re-broadcast
    pal::logf(pal::LogLevel::Info, "deathlink: applying inbound death (PlayerDie)");
}

} // namespace mth
