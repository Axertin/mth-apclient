#pragma once

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

// Owns the game-hook plumbing (GameHooks) + the 10 feature hooks, and drives their slice
// of the per-frame tick. Thin: wiring + lifetime + enforcement dispatch, no new logic.
class HookManager
{
  public:
    HookManager(IGameEvents &events, RandoBridge &rando, ApState &state, std::function<void()> send_death, std::function<void *()> get_player);
    ~HookManager();

    HookManager(const HookManager &) = delete;
    HookManager &operator=(const HookManager &) = delete;

    void tick(ApState &state, SessionPolicy &policy, int save_game_slot); // -1 when no save state
    void drain();                                                         // World::Update pre-hook window
    void kill_player();

    [[nodiscard]] int captured_ap_slot() const;
    void set_ap_slot(int slot);

    void set_modifier_live(int idx, bool on);
    void set_modifiers_armed(bool armed);
    void remove_lock(int slot);
    void set_stat_caps(int attack, int defense, int sidearm);
    void set_ability_randomized(Ability a, bool on);

    void append_status_lines(std::vector<std::string> &out) const;

  private:
    void seed_kear_blocks(ApState &state); // received kear-block items -> LockRegistry removals

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
};

} // namespace mth
