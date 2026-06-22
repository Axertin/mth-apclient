#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "mth/core/ap_save_state.hpp"
#include "mth/core/ap_state.hpp"
#include "mth/core/command_sink.hpp"
#include "mth/core/session_policy.hpp"
#include "mth/core/upgrade_state.hpp"
#include "mth/hooks/death_hooks.hpp"
#include "mth/hooks/levelcap_hooks.hpp"
#include "mth/hooks/modifier_hooks.hpp"

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
class IApLink;
class ApCoordinator;
class InboundGranter;
class PlayerTracker;
class RoomTracker;
class ItemGranter;
class RandoBridge;
class AreaReporter;
class LocationHooks;
class BossHooks;
class LockHooks;
class ChestHooks;
class DevConsole;

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
    [[nodiscard]] std::vector<std::string> status_lines() const override;
    [[nodiscard]] std::vector<std::string> item_lines() const override;
    void give_item(std::int64_t ap_item_id) override;
    void remove_lock(int slot) override;
    void set_modifier(int idx, bool on) override;
    void lock_modifiers(bool armed) override;
    void set_stat_caps(int attack, int defense, int sidearm) override;

  private:
    void ensure_inbound_ready();     // lazily builds save_state_/inbound_ once connected
    void seed_kear_blocks_from_ap(); // received kear-block items -> LockRegistry removals (idempotent)
    // Destruction order: feature hooks first (remove game hooks), then granter_/tracker_,
    // then events_/hooks_, area_reporter_, coordinator_, link_ (stops net thread), then state_.
    ApState state_;
    std::unique_ptr<IApLink> link_;
    std::unique_ptr<ApCoordinator> coordinator_;
    std::unique_ptr<AreaReporter> area_reporter_;
    std::unique_ptr<IGameEvents> events_;
    std::unique_ptr<GameHooks> hooks_;
    std::unique_ptr<PlayerTracker> tracker_;
    std::unique_ptr<RoomTracker> room_tracker_;
    std::unique_ptr<ItemGranter> granter_;
    std::unique_ptr<RandoBridge> rando_;
    std::unique_ptr<LocationHooks> location_hooks_;
    std::unique_ptr<BossHooks> boss_hooks_;
    std::unique_ptr<LockHooks> lock_hooks_;
    std::unique_ptr<ChestHooks> chest_hooks_;
    std::unique_ptr<DeathHooks> death_hooks_;
    std::unique_ptr<ModifierHooks> modifier_hooks_;
    std::unique_ptr<LevelCapHooks> level_cap_hooks_;
    std::optional<ApSaveState> save_state_;
    std::unique_ptr<InboundGranter> inbound_;
    std::atomic<bool> pending_inbound_death_{false};
    bool first_tick_logged_{false};
    SessionPolicy policy_;
    UpgradeState upgrades_;
#ifdef MTHAP_HAS_OVERLAY
    std::unique_ptr<pal::IOverlay> overlay_;
    std::unique_ptr<DevConsole> console_;
#endif
};

} // namespace mth
