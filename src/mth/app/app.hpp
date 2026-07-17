#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "mth/core/ap/ap_save_state.hpp"
#include "mth/core/ap/ap_state.hpp"
#include "mth/core/command_sink.hpp"
#include "mth/core/connect_resend_gate.hpp"
#include "mth/core/data/ability_ids.hpp"
#include "mth/core/scout_registry.hpp"
#include "mth/core/session_policy.hpp"
#include "mth/core/upgrade_state.hpp"
#include "mth/core/wallet_cap_state.hpp"

#ifdef MTHAP_HAS_OVERLAY
namespace pal
{
class IOverlay;
}
#endif

namespace mth
{

struct IGameEvents;
class HookManager;
class ApSession;
class PlayerTracker;
class RoomTracker;
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

    // False until the ctor finishes wiring every member. The tick sink checks this before forwarding any
    // game-thread event, so a hook that goes live mid-construction can't reach a half-built App.
    [[nodiscard]] bool ready() const noexcept
    {
        return ready_.load();
    }

    // Scouted shop-location info (item/player), filled from ApScoutInfo events on the game thread.
    [[nodiscard]] mth::ScoutRegistry &scout_registry()
    {
        return scout_registry_;
    }

    void drive_tick();       // called by tick sink each fixed update (only once ready())
    void drain_grants();     // called by tick sink from World::Update pre-hook (only once ready())
    void on_world_destroy(); // called by tick sink on World teardown (only once ready()); drops the cached Player*

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
    void ensure_inbound_ready(); // lazily builds save_state_ + the grant pipeline's inbound granter once connected
    // Drain ApState's server-reported checked locations into the save-state checked set (Collect / coop),
    // once inbound is ready. Never re-sends; persists once per pass. Called each drive_tick.
    void reconcile_server_checked();
    void apply_vial_capacity(); // push the AP vial count through the offset-free mod-API accessors
    void enforce_wallet_cap();  // clamp live bones to the AP wallet cap (#112); no-op unless authed + wallet_cap
    // Destruction order: overlay first, then hooks_ (game hooks stop first inside the manager),
    // grants_, tracker_/room_tracker_, events_ (AppTickSink, after hooks_), net_ (stops net thread last).
    ApState state_;
    std::unique_ptr<ApSession> net_;
    std::unique_ptr<IGameEvents> events_;
    std::unique_ptr<PlayerTracker> tracker_;
    std::unique_ptr<RoomTracker> room_tracker_;
    std::unique_ptr<HookManager> hooks_;
    std::optional<ApSaveState> save_state_;
    std::unique_ptr<GrantPipeline> grants_;
    std::atomic<bool> pending_inbound_death_{false};
    // Gates the tick entry points until construction finishes. The game-thread tick hooks go live mid-ctor
    // (GameHooks installs Game::FixedUpdate before hooks_ is even assigned), so on a fast-initializing host
    // the first tick can land on a half-built App and deref a null member. Release on the last ctor line,
    // acquire at the top of each tick; cleared first in the dtor so teardown can't be ticked either.
    std::atomic<bool> ready_{false};
    bool first_tick_logged_{false};
    SessionPolicy policy_;
    UpgradeState upgrades_;
    WalletCapState wallet_;
    ConnectResendGate resend_gate_; // fires flush() once per (re)connection
    mth::ScoutRegistry scout_registry_;
#ifdef MTHAP_HAS_OVERLAY
    std::unique_ptr<pal::IOverlay> overlay_;
    std::unique_ptr<OverlayRoot> overlay_root_;
#endif
};

} // namespace mth
