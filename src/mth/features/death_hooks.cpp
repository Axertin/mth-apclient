#include "mth/features/death_hooks.hpp"

#include <utility>

#include "mod/mod_api.hpp"
#include "mth/core/data/game_layout.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_mem.hpp"

namespace
{

// Player+0x1380: the once-per-death guard byte the game sets when a death begins (0 = alive/fresh). Reading
// it is the authoritative "is dying" signal the deathlink edge keys on. Offset, not a function sig.
[[nodiscard]] bool is_dying(void *player)
{
    return player != nullptr && *reinterpret_cast<unsigned char *>(static_cast<char *>(player) + mth::layout::kPlayerDeathGuardOff) != 0;
}

// ycWorld+0x1758 is the world's AreaManager. Player::InitDeath walks player->entity->world->area and
// dereferences it without the null check every other engine site applies, so an area-less world makes a
// requested death fatal.
[[nodiscard]] bool world_area_missing()
{
    // Null covers both no live world and a build whose API cannot supply one; neither is a reason to refuse
    // a deathlink, and the caller's null-player check already holds the transition case.
    void *world = mod::player_world();
    if (world == nullptr)
        return false;
    if (!pal::pointer_looks_valid(world))
        return true; // a world we cannot vouch for is not one to request a death in
    return *reinterpret_cast<void **>(static_cast<char *>(world) + mth::layout::kWorldAreaManagerOff) == nullptr;
}

} // namespace

namespace mth
{

DeathHooks::DeathHooks(std::function<void(const std::string &)> on_local_death, std::function<void *()> get_player)
    : on_local_death_(std::move(on_local_death)), get_player_(std::move(get_player))
{
}

DeathHooks::~DeathHooks() = default;

// PlayerDie only queues damage for the world's gameplay queues, so a requested death cannot land on a tick
// where those did not run. Two freezes stop them and neither is visible to the other: a paused world (any
// menu; World::Update then runs only its pause queue), and a paused game (the whole world queue is skipped,
// which stalls the room clock). A build whose API lacks both accessors falls back to wall ticks rather than
// freezing the gate.
// They are not equally safe to kill through, so record which one this was: a paused world holds a requested
// death until the menu closes and then runs it, while a stalled room clock is a transition or an in-progress
// death sequence, where it lands at an unpredictable point.
bool DeathHooks::gameplay_advanced()
{
    if (mod::pause_api_available() && mod::world_is_paused())
    {
        room_clock_stalled_ = false;
        return false;
    }
    if (!mod::room_time_api_available())
    {
        room_clock_stalled_ = false;
        return true;
    }
    const float now = mod::room_time();
    const bool moved = now != last_room_time_;
    last_room_time_ = now;
    room_clock_stalled_ = !moved;
    return moved;
}

void DeathHooks::poll()
{
    const bool advanced = gameplay_advanced(); // sampled every tick: last_room_time_ must not go stale
    drive_pending_death(advanced);
    void *p = get_player_ ? get_player_() : nullptr;
    if (p == nullptr)
    {
        // No player (world/screen transition): reset the settled-respawn debounce, so an inbound PlayerDie is
        // not applied the instant the player reappears.
        gate_.observe(false, false, advanced);
        return;
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

    const bool fresh_local_death = gate_.observe(dying, alive, advanced);
    if (gate_.take_suppressed_death())
        pal::logf(pal::LogLevel::Info, "deathlink: local death suppressed (echo of a received deathlink)");
    if (fresh_local_death && on_local_death_)
    {
        // Only broadcast a sparkless demise (0 sparks at death, per the pre-death snapshot).
        if (last_alive_spark_ > 0)
        {
            pal::logf(pal::LogLevel::Info, "deathlink: local death suppressed (had %d spark(s); not sparkless)", last_alive_spark_);
            return;
        }
        pal::logf(pal::LogLevel::Info, "deathlink: sparkless local death -> broadcasting");
        // One detail for every death until the death path can distinguish them.
        on_local_death_("had a skill issue");
    }
}

// Returns false only for a block that clears on its own (no player yet, or not settled), which is the caller's
// cue to retry. A missing PlayerDie API and an area-less world cannot clear usefully, so both count as
// handled rather than retried forever.
bool DeathHooks::try_apply_inbound_death()
{
    void *p = get_player_ ? get_player_() : nullptr;
    if (p == nullptr)
        return false; // world/screen transition: the next room's player is moments away
    // Only apply PlayerDie from a settled state (stably alive, not mid-death). Applying it during the
    // Underlab->overworld transition softlocks, and applying it while already dying just no-ops. A stalled
    // room clock with the world unpaused is a transition or a death already in flight, where PlayerDie lands
    // at an unpredictable point; a paused world is a menu, which holds the death safely. #125.
    if (!gate_.stably_alive() || room_clock_stalled_)
        return false;
    // The ending sequence leaves a live, settled player in a World with no area bound, and PlayerDie there
    // faults a tick later inside Player::InitDeath. Checked after the two guards above so that the other
    // area-less window - between area teardown and AreaManagerNewArea - is still caught as the transition it
    // is and retried; what reaches here is settled and area-less, which does not clear on its own. Dropped
    // rather than latched: by the credits the run is over, and a bounce held until an area exists again
    // lands in the post-credits handoff instead of on the run it was meant for. An in-run pause keeps the
    // room's world and its area, so a menu deathlink still queues normally (#125).
    if (world_area_missing())
    {
        pal::logf(pal::LogLevel::Warn, "deathlink: inbound death dropped (world has no area bound; gamestate %d)", mod::current_game_state());
        return true;
    }
    if (!mod::player_die())
    {
        pal::logf(pal::LogLevel::Warn, "deathlink: inbound death not applied (PlayerDie API unavailable)");
        return true;
    }
    // Arm the grace on the tick the death actually goes out, not the tick it was received: the alive polls
    // that follow belong to THIS request and must not settle us before it lands.
    gate_.note_inbound_death_applied();
    pal::logf(pal::LogLevel::Info, "deathlink: applying inbound death (PlayerDie)");
    return true;
}

// Retry a latched inbound death, and age the window it has to land in. Ages on gameplay ticks only: a menu
// stops the game, and the pause must not spend a window meant for gameplay time (#164).
void DeathHooks::drive_pending_death(bool advanced)
{
    if (pending_kill_ticks_ <= 0)
        return;
    // A death that registered after the bounce was latched serves it. kill() checks this too, but the guard
    // byte takes ~0.7s to appear, so a bounce can be latched while the death is still invisible; without this
    // the latch outlives the respawn and kills the player a second time for one exchange.
    if (gate_.death_in_progress())
    {
        pal::logf(pal::LogLevel::Info, "deathlink: latched inbound death served by the death already in progress");
        pending_kill_ticks_ = 0;
        return;
    }
    if (try_apply_inbound_death())
    {
        pending_kill_ticks_ = 0;
        return;
    }
    if (advanced && --pending_kill_ticks_ == 0)
        pal::logf(pal::LogLevel::Warn, "deathlink: inbound death dropped (player never settled within the retry window)");
}

void DeathHooks::kill()
{
    // Suppress our outbound from the moment a bounce arrives, even when applying it is deferred below: this
    // is what breaks the multiworld echo storm (#125).
    gate_.note_inbound_death_received();
    if (gate_.death_in_progress())
    {
        pal::logf(pal::LogLevel::Info, "deathlink: inbound death served by the death already in progress");
        return;
    }
    if (try_apply_inbound_death())
    {
        pending_kill_ticks_ = 0; // a queued-but-unlanded death is the game's problem now, not ours to retry
        return;
    }
    // One latch however many bounces arrive: a storm must not chain-kill the player through several
    // respawns. The window runs from the FIRST deferral, so a steady stream cannot push the deadline out
    // forever (#164).
    if (pending_kill_ticks_ > 0)
    {
        pal::logf(pal::LogLevel::Debug, "deathlink: inbound death arrived while one is already pending (coalesced)");
        return;
    }
    const bool no_player = (get_player_ ? get_player_() : nullptr) == nullptr;
    pal::logf(pal::LogLevel::Info, "deathlink: inbound death deferred (%s), retrying",
              no_player             ? "no player captured"
              : room_clock_stalled_ ? "room clock stalled: mid-transition"
                                    : "player not settled");
    pending_kill_ticks_ = kPendingInboundDeathTicks;
}

void DeathHooks::reset()
{
    pending_kill_ticks_ = 0;
}

} // namespace mth
