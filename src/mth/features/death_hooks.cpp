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
    {
        gate_.observe(false, false); // no player (world/screen transition): reset the settled-respawn debounce
        return;                      // so an inbound PlayerDie is not applied the instant the player reappears
    }
    const bool dying = is_dying(p);
    // Re-arm only on a true respawn: the guard byte pulses through the death sequence, so use health > 0 as
    // the stable "alive" signal. Fall back to the guard byte if the health API is unavailable.
    const bool alive = mod::health_api_available() ? (mod::player_health() > 0.0f) : !dying;

    // Player::DropDeathSpark zeroes the live spark mid-death, before this poll sees the edge, so read it while
    // alive instead. Gate on `alive` not `!dying`: the guard byte pulses mid-death while health stays 0, and a
    // pulse must not resample the already-dropped spark. Spark API absent -> hold 0 (still broadcasts).
    if (alive)
        last_alive_spark_ = mod::spark_api_available() ? mod::player_spark() : 0;

    if (gate_.observe(dying, alive) && on_local_death_)
    {
        // Only broadcast a sparkless demise (0 sparks at death, per the pre-death snapshot).
        if (last_alive_spark_ > 0)
        {
            pal::logf(pal::LogLevel::Info, "deathlink: local death suppressed (had %d spark(s); not sparkless)", last_alive_spark_);
            return;
        }
        pal::logf(pal::LogLevel::Info, "deathlink: sparkless local death -> broadcasting");
        on_local_death_();
    }
}

void DeathHooks::kill()
{
    // Every received inbound death suppresses our own outbound until we settle, even if we defer applying it
    // below: this is what breaks the multiworld echo storm (#125).
    gate_.note_inbound_death();
    void *p = get_player_ ? get_player_() : nullptr;
    if (p == nullptr)
    {
        pal::logf(pal::LogLevel::Warn, "deathlink: inbound death not applied (player not captured yet)");
        return;
    }
    // Only apply PlayerDie from a settled state (stably alive, not mid-death/mid-transition). Applying it
    // during the Underlab->overworld transition softlocks, and applying it while already dying just no-ops;
    // a death that arrives before we settle is dropped (deathlink is best-effort). #125.
    if (!gate_.stably_alive())
    {
        pal::logf(pal::LogLevel::Info, "deathlink: inbound death deferred (player not settled: mid-death/transition)");
        return;
    }
    if (!mod::player_die())
    {
        pal::logf(pal::LogLevel::Warn, "deathlink: inbound death not applied (PlayerDie API unavailable)");
        return;
    }
    pal::logf(pal::LogLevel::Info, "deathlink: applying inbound death (PlayerDie)");
}

} // namespace mth
