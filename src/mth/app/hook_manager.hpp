#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "mth/core/data/ability_ids.hpp"

namespace mth
{

struct IGameEvents;
class GameHooks;
class RandoBridge;
class ScoutRegistry;
class ApState;
class SessionPolicy;
class LocationHooks;
class BossHooks;
class GoalTracker;
class LockHooks;
class ChestHooks;
class DeathHooks;
class AbilityHooks;
class PawnShopHooks;
class ModifierHooks;
class LevelCapHooks;
class FountainLampHooks;

// Owns the game-hook plumbing (GameHooks) + the 11 feature hooks, and drives their slice
// of the per-frame tick. Thin: wiring + lifetime + enforcement dispatch, no new logic.
class HookManager
{
  public:
    HookManager(IGameEvents &events, RandoBridge &rando, ScoutRegistry &scout, ApState &state, std::function<void()> send_death,
                std::function<void *()> get_player);
    ~HookManager();

    HookManager(const HookManager &) = delete;
    HookManager &operator=(const HookManager &) = delete;

    void tick(ApState &state, SessionPolicy &policy);
    void drain();            // World::Update pre-hook window
    void on_world_destroy(); // re-arm native collected-bit enforcement (save reload clears it)
    void kill_player();

    [[nodiscard]] int captured_ap_slot() const;
    void set_ap_slot(int slot);

    void set_modifier_live(int idx, bool on);
    void set_modifiers_armed(bool armed);
    void remove_lock(int slot);
    void set_stat_caps(int attack, int defense, int sidearm);
    void set_ability_randomized(Ability a, bool on);
    void set_lamp_console_override(std::uint32_t mask); // offline test: OR extra Ossex fountain lamps lit each frame

    void append_status_lines(std::vector<std::string> &out) const;

  private:
    void seed_kear_blocks(ApState &state); // received kear-block items -> LockRegistry removals

    std::atomic<std::uint32_t> lamp_console_override_{0}; // sticky console-forced lamp mask (render thread) OR'd over slot_data in tick (game thread)

    std::unique_ptr<GameHooks> game_hooks_;
    std::unique_ptr<LocationHooks> location_hooks_;
    std::unique_ptr<BossHooks> boss_hooks_;
    std::unique_ptr<GoalTracker> goal_tracker_;
    std::unique_ptr<LockHooks> lock_hooks_;
    std::unique_ptr<ChestHooks> chest_hooks_;
    std::unique_ptr<DeathHooks> death_hooks_;
    std::unique_ptr<AbilityHooks> ability_hooks_;
    std::unique_ptr<PawnShopHooks> pawn_shop_hooks_;
    std::unique_ptr<ModifierHooks> modifier_hooks_;
    std::unique_ptr<LevelCapHooks> level_cap_hooks_;
    std::unique_ptr<FountainLampHooks> fountain_lamp_hooks_;
};

} // namespace mth
