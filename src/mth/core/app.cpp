#include "mth/core/app.hpp"

#include <atomic>
#include <cstdlib>

#include "mth/core/ap_coordinator.hpp"
#include "mth/core/ap_link.hpp"
#include "mth/core/build_id.hpp"
#include "mth/core/game_events.hpp"
#include "mth/core/rando_bridge.hpp"
#include "mth/game_hooks.hpp"
#include "mth/rando_hooks.hpp"
#include "mth_version.h"
#include "pal/pal_hook.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"
#ifdef MTHAP_HAS_NET
#include "mth/net/ap_link_apclient.hpp"
#else
#include "mth/core/null_ap_link.hpp"
#endif

namespace
{

// Tick sink: drives the AP coordinator on the fixed sim tick and logs its
// first fire (so we know the detour is live without flooding at frame rate).
class AppTickSink final : public mth::IGameEvents
{
  public:
    AppTickSink(mth::ApCoordinator &coord, mth::ApState &state) : coord_(coord), state_(state)
    {
    }
    void on_game_fixed_update() override
    {
        if (!logged_.exchange(true, std::memory_order_relaxed))
            pal::logf(pal::LogLevel::Info, "tick: Game::FixedUpdate live; AP coordinator pumping");
        coord_.tick();
        const int n = static_cast<int>(state_.received_items().size());
        for (; logged_items_ < n; ++logged_items_)
        {
            const auto &it = state_.received_items()[static_cast<std::size_t>(logged_items_)];
            pal::logf(pal::LogLevel::Info, "AP recv: item_id=%lld index=%d from=%d (not granted yet)", static_cast<long long>(it.item_id), it.index,
                      it.player_from);
        }
    }

  private:
    mth::ApCoordinator &coord_;
    mth::ApState &state_;
    std::atomic<bool> logged_{false};
    int logged_items_{0};
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

#ifdef MTHAP_HAS_NET
    link_ = std::make_unique<mth::net::ApLink>();
#else
    link_ = std::make_unique<mth::NullApLink>();
#endif
    coordinator_ = std::make_unique<ApCoordinator>(*link_, state_);
    events_ = std::make_unique<AppTickSink>(*coordinator_, state_);
    hooks_ = std::make_unique<GameHooks>(build, *events_);
    rando_ = std::make_unique<RandoBridge>(*link_, state_);
    rando_hooks_ = std::make_unique<RandoHooks>(build, *rando_);

    if (const char *server = std::getenv("MTHAP_AP_SERVER"); server && *server)
    {
        const char *slot = std::getenv("MTHAP_AP_SLOT");
        const char *password = std::getenv("MTHAP_AP_PASSWORD");
        pal::logf(pal::LogLevel::Info, "AP: MTHAP_AP_SERVER set; connecting to %s", server);
        link_->connect(server, slot ? slot : "Player1", password ? password : "");
    }
    else
    {
        pal::logf(pal::LogLevel::Info, "AP: MTHAP_AP_SERVER unset; net idle");
    }
}

App::~App()
{
    rando_hooks_.reset();
    rando_.reset();
    hooks_.reset();
    events_.reset();
    coordinator_.reset();
    link_.reset();
    pal::shutdown_hook_engine();
    pal::logf(pal::LogLevel::Info, "mth-apclient unloading");
}

void App::run()
{
    // Tick detours are installed in the ctor; they fire on the game thread.
    // Nothing else to drive yet - the worker thread returns and the hooks live
    // until App is destroyed. (Networking / per-tick logic land in later work.)
    pal::logf(pal::LogLevel::Info, "App::run -- tick hooks installed; idling");
}

} // namespace mth
