#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "mth/core/ap_save_state.hpp"
#include "mth/core/ap_state.hpp"
#include "mth/game_item_granter.hpp"
#ifdef MTHAP_HAS_OVERLAY
#include "mth/ui/command_sink.hpp"
#endif

namespace pal
{
class IOverlay;
}

namespace mth
{

struct IGameEvents;
class GameHooks;
class IApLink;
class ApCoordinator;
class InboundGranter;
class RandoBridge;
class RandoHooks;
class DevConsole;

// Composition root. Logger and hook engine are PAL globals; App owns everything else.
class App
#ifdef MTHAP_HAS_OVERLAY
    : public ICommandSink
#endif
{
  public:
    App();
    ~App();

    App(const App &) = delete;
    App &operator=(const App &) = delete;

    void run();

    void drive_tick();   // called by tick sink each fixed update
    void drain_grants(); // called by tick sink from World::Update pre-hook

#ifdef MTHAP_HAS_OVERLAY
    void connect(const std::string &server, const std::string &slot, const std::string &password) override;
    void disconnect() override;
    [[nodiscard]] std::vector<std::string> status_lines() const override;
    [[nodiscard]] std::vector<std::string> item_lines() const override;
    void give_item(std::int64_t ap_item_id) override;
#endif

  private:
    void ensure_inbound_ready(); // lazily builds save_state_/inbound_ once connected
    // Destruction order: hooks_, events_, coordinator_, link_ (stops net thread), then state_.
    ApState state_;
    std::unique_ptr<IApLink> link_;
    std::unique_ptr<ApCoordinator> coordinator_;
    std::unique_ptr<IGameEvents> events_;
    std::unique_ptr<GameHooks> hooks_;
    std::unique_ptr<RandoBridge> rando_;
    std::unique_ptr<RandoHooks> rando_hooks_;
    GameItemGranter granter_;
    std::optional<ApSaveState> save_state_;
    std::unique_ptr<InboundGranter> inbound_;
    bool first_tick_logged_{false};
#ifdef MTHAP_HAS_OVERLAY
    std::unique_ptr<pal::IOverlay> overlay_;
    std::unique_ptr<DevConsole> console_;
#endif
};

} // namespace mth
