#include "mth/features/trap_hooks.hpp"

#include <atomic>
#include <cstdio>
#include <mutex>

#include "mod/mod_api.hpp"
#include "mth/core/data/game_state_ids.hpp"
#include "mth/core/data/trap_table.hpp"
#include "mth/core/death_broadcast_gate.hpp" // kMaxFixedUpdateHz, for the no-delta fallback
#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"

namespace mth
{

namespace
{

// Written from the named FixedUpdate hook at the top of the frame and read by tick() after the
// original has run. Same thread today, and an atomic costs nothing for the guarantee.
std::atomic<float> g_frame_delta{0.0f};
std::atomic<bool> g_frame_delta_seen{false};

void on_fixed_update_delta(float elapsed)
{
    g_frame_delta.store(elapsed, std::memory_order_relaxed);
    g_frame_delta_seen.store(true, std::memory_order_relaxed);
}

// Only reached when the named hook never fires. FixedUpdate runs at 60 or 120 Hz with nothing to
// tell them apart, so assume the faster rate: a trap that overstays beats one cut in half.
constexpr float kFallbackDelta = 1.0f / static_cast<float>(kMaxFixedUpdateHz);

} // namespace

TrapHooks::TrapHooks()
{
    mod::set_fixed_update_delta_hook(&on_fixed_update_delta);
}

TrapHooks::~TrapHooks()
{
    clear();
}

TrapArm TrapHooks::arm(int modifier_index, float seconds_override)
{
    const TrapDef *def = trap_for_modifier(modifier_index);
    if (def == nullptr)
    {
        pal::logf(pal::LogLevel::Warn, "traps: modifier %d is not a trap; nothing to arm", modifier_index);
        return TrapArm::Unavailable;
    }
    if (!pal::runtime_modifiers_available())
    {
        pal::logf(pal::LogLevel::Warn, "traps: \"%s\" (idx=%d) cannot be armed (g_cheatManager unavailable on this build); consumed", def->label,
                  modifier_index);
        return TrapArm::Unavailable;
    }
    if (!is_gameplay_game_state(mod::current_game_state()))
    {
        // runtime_modifier_ready() below is a one-way latch (the game never clears it once a save has
        // been activated), so it cannot tell the title screen from an active run. The gamestate range
        // can, and RoomTracker already keys the same distinction on it.
        if (notready_warned_.insert(modifier_index).second)
            pal::logf(pal::LogLevel::Warn, "traps: \"%s\" (idx=%d) deferred until the player is in a room; retrying", def->label, modifier_index);
        return TrapArm::NotReady;
    }
    if (!pal::runtime_modifier_ready())
    {
        // The native read below resolves the live save slot itself and does not null-check it, so
        // asking before a save is activated would fault. The connection binds at the title screen,
        // so a trap really can arrive with no save loaded.
        if (notready_warned_.insert(modifier_index).second)
            pal::logf(pal::LogLevel::Warn, "traps: \"%s\" (idx=%d) deferred until a save is active; retrying", def->label, modifier_index);
        return TrapArm::NotReady;
    }
    if (mod::cheat_manager_is_cheat_applied(modifier_index))
    {
        // Arming over a modifier the player chose would be invisible for the duration and would then
        // "expire" by taking their setting away, with ActivateSaveCheats fighting to restore it.
        pal::logf(pal::LogLevel::Info, "traps: \"%s\" (idx=%d) already on by the player's own choice; consumed without arming", def->label, modifier_index);
        return TrapArm::Skipped;
    }
    if (!pal::set_runtime_modifier(modifier_index, true))
    {
        // Logged once per stall rather than every retry tick: the granter calls arm() again next
        // tick regardless, so a warning here would repeat until the write finally goes through.
        // Traps can stall on distinct indices at once, so this is a set rather than one scalar;
        // insert() only warns the first time a given index joins the stalled set.
        if (notready_warned_.insert(modifier_index).second)
            pal::logf(pal::LogLevel::Warn, "traps: \"%s\" (idx=%d) write refused this frame; retrying", def->label, modifier_index);
        return TrapArm::NotReady;
    }
    notready_warned_.erase(modifier_index); // the write went through, so a future stall on this index warns again
    // A prior lift for this same index may still be sitting in the retry list; a fresh arm wins over
    // it; otherwise the next tick's retry would turn this brand-new trap straight back off.
    std::erase(pending_off_, modifier_index);
    const float seconds = seconds_override > 0.0f ? seconds_override : def->seconds;
    state_.arm(modifier_index, seconds);
    pal::logf(pal::LogLevel::Info, "traps: \"%s\" (idx=%d) armed for %.1fs", def->label, modifier_index, static_cast<double>(seconds));
    return TrapArm::Armed;
}

void TrapHooks::queue_arm(int modifier_index, float seconds_override)
{
    std::lock_guard<std::mutex> lk(mtx_);
    pending_arm_.emplace_back(modifier_index, seconds_override);
}

void TrapHooks::tick()
{
    // Drained unconditionally: arm() itself refuses outside gameplay, so a queued console trap just
    // comes back NotReady rather than piling up here.
    drain_pending_arm();
    // Retried before the gameplay check below: a lift the game refused earlier has to keep trying to
    // land even while the player is at a menu, or the modifier stays forced on indefinitely.
    retry_pending_off();
    if (!is_gameplay_game_state(mod::current_game_state()))
    {
        // Leaving gameplay ends every running trap outright rather than freezing its countdown: the
        // room clock that ages it has stalled here too, so a suspend-and-resume would just leave the
        // bit stuck on with nothing left to lift it. Guarded on non-empty so an idle menu does not
        // call clear() every frame: clear() also resets notready_warned_, and doing that every tick
        // would defeat its dedup and make arm()'s "deferred until the player is in a room" warning
        // (logged right after this by grants_->tick()) spam once a frame instead of once per stall.
        if (!state_.empty())
            clear();
        publish_active();
        return;
    }
    advance_frame();
    publish_active();
}

void TrapHooks::drain_pending_arm()
{
    std::vector<std::pair<int, float>> batch;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        batch.swap(pending_arm_);
    }
    for (auto [idx, seconds] : batch)
        arm(idx, seconds);
}

void TrapHooks::publish_active()
{
    std::lock_guard<std::mutex> lk(mtx_);
    snapshot_ = state_.active_with_remaining();
}

void TrapHooks::advance_frame()
{
    // tick() already ran retry_pending_off() for this frame before reaching here.
    if (state_.empty())
        return;
    const float dt = gameplay_advanced() ? take_delta() : 0.0f;
    for (int idx : state_.advance(dt))
    {
        const TrapDef *def = trap_for_modifier(idx);
        if (pal::set_runtime_modifier(idx, false))
        {
            pal::logf(pal::LogLevel::Info, "traps: \"%s\" (idx=%d) expired", def != nullptr ? def->label : "?", idx);
        }
        else
        {
            pending_off_.push_back(idx);
            pal::logf(pal::LogLevel::Warn, "traps: \"%s\" (idx=%d) expired but the lift failed; still forced on, retrying", def != nullptr ? def->label : "?",
                      idx);
        }
    }
    if (dt <= 0.0f)
        return;
    // ActivateSaveCheats rebuilds the runtime mask from the save, so a load, a profile select, or
    // the player opening the options menu would otherwise cancel a trap that is still running.
    for (int idx : state_.active())
        pal::set_runtime_modifier(idx, true);
}

void TrapHooks::clear()
{
    notready_warned_.clear(); // a stall carried into the next seed would otherwise never warn again
    for (int idx : state_.clear())
    {
        if (pal::set_runtime_modifier(idx, false))
            continue;
        pending_off_.push_back(idx);
        const TrapDef *def = trap_for_modifier(idx);
        pal::logf(pal::LogLevel::Warn, "traps: \"%s\" (idx=%d) lift failed on clear; still forced on, retrying", def != nullptr ? def->label : "?", idx);
    }
}

// Retries every lift the game refused earlier. Run once at the top of every tick rather than
// looped or blocked on here: pal::set_runtime_modifier's false means "not right now", and the next
// tick is the next chance to ask again.
void TrapHooks::retry_pending_off()
{
    if (pending_off_.empty() || !pal::runtime_modifier_ready())
        return; // the cheat-applied read below faults without an activated save slot
    std::erase_if(pending_off_,
                  [](int idx)
                  {
                      const TrapDef *def = trap_for_modifier(idx);
                      if (mod::cheat_manager_is_cheat_applied(idx))
                      {
                          // The save carries that bit now, so between the failed lift and this retry
                          // the player turned the modifier on themselves; clearing it would take
                          // their own setting away.
                          pal::logf(pal::LogLevel::Info, "traps: \"%s\" (idx=%d) is the player's own choice now; lift retry dropped",
                                    def != nullptr ? def->label : "?", idx);
                          return true;
                      }
                      if (!pal::set_runtime_modifier(idx, false))
                          return false;
                      pal::logf(pal::LogLevel::Info, "traps: \"%s\" (idx=%d) lift retry succeeded", def != nullptr ? def->label : "?", idx);
                      return true;
                  });
}

// Whether the world's gameplay queues ran this frame. A trap must not burn its duration while the
// player sits in a menu, and must not lose time to a room transition.
bool TrapHooks::gameplay_advanced()
{
    if (mod::pause_api_available() && mod::world_is_paused())
        return false;
    if (!mod::room_time_api_available())
        return true;
    // Only whether the clock moved matters, not by how much, so sampling it while no trap is running
    // would buy nothing; the first tick after arming compares against a stale reading and can lose
    // that one frame.
    const float now = mod::room_time();
    const bool moved = now != last_room_time_;
    last_room_time_ = now;
    return moved;
}

float TrapHooks::take_delta()
{
    const bool have = g_frame_delta_seen.load(std::memory_order_relaxed);
    if (!timing_source_logged_)
    {
        timing_source_logged_ = true;
        if (have)
            pal::logf(pal::LogLevel::Info, "traps: timing from the FixedUpdate frame delta");
        else
            pal::logf(pal::LogLevel::Warn, "traps: no FixedUpdate delta on this build; timing at an assumed %d Hz", kMaxFixedUpdateHz);
    }
    return have ? g_frame_delta.load(std::memory_order_relaxed) : kFallbackDelta;
}

std::vector<std::string> TrapHooks::status_lines() const
{
    std::vector<ActiveTrap> running;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        running = snapshot_;
    }
    std::vector<std::string> out;
    if (running.empty())
    {
        out.push_back("traps: none active");
        return out;
    }
    for (const ActiveTrap &t : running)
    {
        const TrapDef *def = trap_for_modifier(t.index);
        char secs[16];
        std::snprintf(secs, sizeof(secs), "%.1f", static_cast<double>(t.remaining));
        out.push_back(std::string("trap active: ") + (def != nullptr ? def->label : "?") + " (idx " + std::to_string(t.index) + ", " + secs + "s left)");
    }
    return out;
}

} // namespace mth
