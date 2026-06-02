#pragma once

#include <memory>
#include <string>
#include <vector>

#include "mth/core/ap_state.hpp"
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
class RandoBridge;
class RandoHooks;
class DevConsole;

// Composition root. Owns the object graph for the mod. Created once by
// pal::apclient_main on the worker thread. The logger and hook engine are
// PAL globals (not owned here); App owns everything else as it grows.
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

    // Runs the mod. At base scope this logs and returns; later milestones
    // drive the per-frame loop and own net/game-memory/overlay collaborators.
    void run();

#ifdef MTHAP_HAS_OVERLAY
    void connect(const std::string &server, const std::string &slot, const std::string &password) override;
    void disconnect() override;
    [[nodiscard]] std::vector<std::string> status_lines() const override;
    [[nodiscard]] std::vector<std::string> item_lines() const override;
#endif

  private:
    // Declared so destruction nests safely: hooks_ removed first, then sink,
    // then coordinator, then link (stops the net thread), then state.
    ApState state_;
    std::unique_ptr<IApLink> link_;
    std::unique_ptr<ApCoordinator> coordinator_;
    std::unique_ptr<IGameEvents> events_;
    std::unique_ptr<GameHooks> hooks_;
    std::unique_ptr<RandoBridge> rando_;
    std::unique_ptr<RandoHooks> rando_hooks_;
#ifdef MTHAP_HAS_OVERLAY
    std::unique_ptr<pal::IOverlay> overlay_;
    std::unique_ptr<DevConsole> console_;
#endif
};

} // namespace mth
