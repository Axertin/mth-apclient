#pragma once

namespace mth
{

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
};

} // namespace mth
