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
class BossTracker;
class GoalTracker;
class LockHooks;
class ChestHooks;
class DeathHooks;
class AbilityHooks;
class PawnShopHooks;
class SewerCatGate;
class ModifierHooks;
class LevelCapHooks;
class FountainLampHooks;
class TitleGate;
class SaveTakeover;

// Owns the game-hook plumbing (GameHooks) + the 12 feature hooks, and drives their slice
// of the per-frame tick. Thin: wiring + lifetime + enforcement dispatch, no new logic.
class HookManager
{
  public:
    HookManager(IGameEvents &events, RandoBridge &rando, ScoutRegistry &scout, ApState &state, std::function<void()> send_death,
                std::function<void *()> get_player);
    ~HookManager();

    HookManager(const HookManager &) = delete;
    HookManager &operator=(const HookManager &) = delete;

    void tick(ApState &state, SessionPolicy &policy, int save_game_slot); // -1 when no save state
    // Session boundary: drop the feature state seeded from one connection's received items (lock removals)
    // and the captured AP-game slot, so none of it carries into the next connection's save.
    void clear_session_state();
    void drain();                          // World::Update pre-hook window
    void on_world_update_end(void *world); // menu-world scene walk for the save takeover
    void on_world_destroy();               // re-arm native collected-bit enforcement (save reload clears it)
    void kill_player();
    bool credit_kear_key(); // vanilla kear mode: grant one usable key to the live player (#130); false if not ready

    [[nodiscard]] int captured_ap_slot() const;
    void set_ap_slot(int slot);

    void set_modifier_live(int idx, bool on);
    void set_modifiers_armed(bool armed);
    void remove_lock(int slot);
    void set_stat_caps(int attack, int defense, int sidearm);
    void set_ability_randomized(Ability a, bool on);
    void set_lamp_console_override(std::uint32_t mask); // offline test: OR extra Ossex fountain lamps lit each frame

    // True while a save-takeover session is live; gates the dev console's save-write toggle.
    [[nodiscard]] bool takeover_active() const;

    void append_status_lines(std::vector<std::string> &out) const;

  private:
    void seed_kear_blocks(ApState &state); // received kear-block items -> LockRegistry removals
    // Clamp weapon ownership to the tiers AP granted. Bound AP save only: it is a durable, destructive write.
    void enforce_weapon_grants(ApState &state, bool authed, bool slot_ok);

    std::atomic<std::uint32_t> lamp_console_override_{0}; // sticky console-forced lamp mask (render thread) OR'd over slot_data in tick (game thread)
    // Set the first time a save sees an AP session, cleared only on save load. Vendors that sell outside
    // AP logic latch on this rather than the live phase, so a mid-run disconnect cannot reopen them.
    std::atomic<bool> vendor_lockout_{false};

    std::uintptr_t save_manager_{0};     // g_saveManager, for the per-tick starter-weapon-swap clear
    RandoBridge &rando_;                 // checked-location state; the donation machine reads it (#162)
    std::function<void *()> get_player_; // live Player* accessor (shared with DeathHooks + kear credit)
    std::unique_ptr<GameHooks> game_hooks_;
    std::unique_ptr<LocationHooks> location_hooks_;
    std::unique_ptr<BossTracker> boss_tracker_;
    std::unique_ptr<GoalTracker> goal_tracker_;
    std::unique_ptr<LockHooks> lock_hooks_;
    std::unique_ptr<ChestHooks> chest_hooks_;
    std::unique_ptr<DeathHooks> death_hooks_;
    std::unique_ptr<AbilityHooks> ability_hooks_;
    std::unique_ptr<PawnShopHooks> pawn_shop_hooks_;
    std::unique_ptr<SewerCatGate> sewer_cat_gate_;
    std::unique_ptr<ModifierHooks> modifier_hooks_;
    std::unique_ptr<LevelCapHooks> level_cap_hooks_;
    std::unique_ptr<FountainLampHooks> fountain_lamp_hooks_;
    std::unique_ptr<SaveTakeover> save_takeover_; // before TitleGate: it holds the detour that calls in
    std::unique_ptr<TitleGate> title_gate_;
};

} // namespace mth
