#include "mth/core/app.hpp"

#include <atomic>

#include "mth/core/build_id.hpp"
#include "mth/core/game_events.hpp"
#include "mth/game_hooks.hpp"
#include "mth_version.h"
#include "pal/pal_hook.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

namespace
{

// Proof-of-life sink: logs the first time each tick fires (subsequent fires
// suppressed so we don't flood at frame rate). Confirms each detour is live on
// the real game without becoming log spam.
class LoggingGameEvents final : public mth::IGameEvents
{
  public:
    void on_game_fixed_update() override
    {
        first(fixed_, "Game::FixedUpdate");
    }
    void on_game_update(float dt) override
    {
        first_dt(update_, "Game::Update", dt);
    }
    void on_world_update() override
    {
        first(world_, "World::Update");
    }
    void on_update_queue(float dt) override
    {
        first_dt(queue_, "ycUpdateQueue::Update", dt);
    }

  private:
    static void first(std::atomic<bool> &flag, const char *what)
    {
        if (!flag.exchange(true, std::memory_order_relaxed))
            pal::logf(pal::LogLevel::Info, "tick: %s fired (further fires suppressed)", what);
    }
    static void first_dt(std::atomic<bool> &flag, const char *what, float dt)
    {
        if (!flag.exchange(true, std::memory_order_relaxed))
            pal::logf(pal::LogLevel::Info, "tick: %s fired dt=%.5f (further fires suppressed)", what, static_cast<double>(dt));
    }

    std::atomic<bool> fixed_{false};
    std::atomic<bool> update_{false};
    std::atomic<bool> world_{false};
    std::atomic<bool> queue_{false};
};

} // namespace

namespace mth
{

App::App()
{
    pal::logf(pal::LogLevel::Info, "mth-apclient %.*s loaded", static_cast<int>(version::string.size()), version::string.data());

    const auto game = pal::game_module();
    const auto self = pal::self_module();
    const auto bid = pal::game_build_id();
    const auto build = detect_build(bid);
    const auto name = build_name(build);

    pal::logf(pal::LogLevel::Info, "game base=0x%llx size=0x%zx path=%s", static_cast<unsigned long long>(game.base), game.size, game.path.c_str());
    pal::logf(pal::LogLevel::Info, "self base=0x%llx path=%s", static_cast<unsigned long long>(self.base), self.path.c_str());
    pal::logf(pal::LogLevel::Info, "build_id=%s build=%.*s", bid.c_str(), static_cast<int>(name.size()), name.data());

    pal::init_hook_engine();

    // Install the engine tick detours for this build and route them to our
    // logging sink. No-op (with a log line) on unmapped builds.
    events_ = std::make_unique<LoggingGameEvents>();
    hooks_ = std::make_unique<GameHooks>(build, *events_);
}

App::~App()
{
    // hooks_ removed first (member declared after events_), then the sink, then
    // the engine is torn down.
    hooks_.reset();
    events_.reset();
    pal::shutdown_hook_engine();
    pal::logf(pal::LogLevel::Info, "mth-apclient unloading");
}

void App::run()
{
    // Tick detours are installed in the ctor; they fire on the game thread.
    // Nothing else to drive yet — the worker thread returns and the hooks live
    // until App is destroyed. (Networking / per-tick logic land in later work.)
    pal::logf(pal::LogLevel::Info, "App::run -- tick hooks installed; idling");
}

} // namespace mth
