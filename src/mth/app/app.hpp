#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "mth/core/ap/ap_save_state.hpp"
#include "mth/core/ap/ap_state.hpp"
#include "mth/core/command_sink.hpp"
#include "mth/core/data/ability_ids.hpp"
#include "mth/core/session_policy.hpp"
#include "mth/core/upgrade_state.hpp"
#include "mth/features/death_hooks.hpp"
#include "mth/features/levelcap_hooks.hpp"
#include "mth/features/modifier_hooks.hpp"

#ifdef MTHAP_HAS_OVERLAY
namespace pal
{
class IOverlay;
}
#endif

namespace mth
{

struct IGameEvents;
class GameHooks;
class ApSession;
class PlayerTracker;
class RoomTracker;
class LocationHooks;
class BossHooks;
class GoalTracker;
class LockHooks;
class ChestHooks;
class AbilityHooks;
class PawnShopHooks;
class OverlayRoot;
class GrantPipeline;

// Composition root. Logger and hook engine are PAL globals; App owns everything else.
// Implements ICommandSink unconditionally (the dev console is the only caller today,
// but the sink itself has no overlay dependency).
class App : public ICommandSink
{
  public:
    App();
    ~App();

    App(const App &) = delete;
    App &operator=(const App &) = delete;

    void run();

    void drive_tick();   // called by tick sink each fixed update
    void drain_grants(); // called by tick sink from World::Update pre-hook

    void connect(const std::string &server, const std::string &slot, const std::string &password) override;
    void disconnect() override;
    [[nodiscard]] ConnectionStatus connection_status() const override;
    [[nodiscard]] std::vector<std::string> status_lines() const override;
    [[nodiscard]] std::vector<std::string> item_lines() const override;
    void give_item(std::int64_t ap_item_id) override;
    void remove_lock(int slot) override;
    void set_modifier(int idx, bool on) override;
    void lock_modifiers(bool armed) override;
    void set_stat_caps(int attack, int defense, int sidearm) override;
    void set_ability_randomized(Ability ability, bool randomized) override;
    void enable_deathlink(bool on) override;

  private:
    void ensure_inbound_ready();     // lazily builds save_state_/inbound_ once connected
    void seed_kear_blocks_from_ap(); // received kear-block items -> LockRegistry removals (idempotent)
    // Destruction order: feature hooks first (remove game hooks), then granter_/tracker_,
    // then events_/hooks_, net_ (stops net thread last), then state_.
    ApState state_;
    std::unique_ptr<ApSession> net_;
    std::unique_ptr<IGameEvents> events_;
    std::unique_ptr<GameHooks> hooks_;
    std::unique_ptr<PlayerTracker> tracker_;
    std::unique_ptr<RoomTracker> room_tracker_;
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
    std::optional<ApSaveState> save_state_;
    std::unique_ptr<GrantPipeline> grants_;
    std::atomic<bool> pending_inbound_death_{false};
    bool first_tick_logged_{false};
    SessionPolicy policy_;
    UpgradeState upgrades_;
#ifdef MTHAP_HAS_OVERLAY
    std::unique_ptr<pal::IOverlay> overlay_;
    std::unique_ptr<OverlayRoot> overlay_root_;
#endif
};

} // namespace mth
