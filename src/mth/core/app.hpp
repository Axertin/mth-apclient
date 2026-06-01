#pragma once

#include <memory>

namespace mth
{

struct IGameEvents;
class GameHooks;

// Composition root. Owns the object graph for the mod. Created once by
// pal::apclient_main on the worker thread. The logger and hook engine are
// PAL globals (not owned here); App owns everything else as it grows.
class App
{
  public:
    App();
    ~App();

    App(const App &) = delete;
    App &operator=(const App &) = delete;

    // Runs the mod. At base scope this logs and returns; later milestones
    // drive the per-frame loop and own net/game-memory/overlay collaborators.
    void run();

  private:
    // Declared before hooks_ so the tick detours are removed (hooks_ destroyed)
    // before the sink they reference goes away.
    std::unique_ptr<IGameEvents> events_;
    std::unique_ptr<GameHooks> hooks_;
};

} // namespace mth
