#include "mth/core/app.hpp"

#include <atomic>
#include <cstdlib>

#include "mth/core/ap_coordinator.hpp"
#include "mth/core/ap_link.hpp"
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
#ifdef MTHAP_HAS_OVERLAY
#include "mth/core/game_symbols.hpp"
#include "mth/ui/dev_console.hpp"
#include "pal/pal_overlay.hpp"
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

    pal::logf(pal::LogLevel::Info, "game base=0x%llx size=0x%zx path=%s", static_cast<unsigned long long>(game.base), game.size, game.path.c_str());
    pal::logf(pal::LogLevel::Info, "self base=0x%llx path=%s", static_cast<unsigned long long>(self.base), self.path.c_str());
    pal::logf(pal::LogLevel::Info, "build_id=%s", bid.c_str());

    pal::init_hook_engine();

#ifdef MTHAP_HAS_NET
    link_ = std::make_unique<mth::net::ApLink>();
#else
    link_ = std::make_unique<mth::NullApLink>();
#endif
    coordinator_ = std::make_unique<ApCoordinator>(*link_, state_);
    events_ = std::make_unique<AppTickSink>(*coordinator_, state_);
    hooks_ = std::make_unique<GameHooks>(*events_);
    rando_ = std::make_unique<RandoBridge>(*link_, state_);
    rando_hooks_ = std::make_unique<RandoHooks>(*rando_);

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
#ifdef MTHAP_HAS_OVERLAY
    {
        const pal::OverlayConfig ocfg{pal::resolve_game_symbol(sym::process_sdl_event)};
        console_ = std::make_unique<DevConsole>(*this);
        overlay_ = pal::make_overlay(ocfg);
        overlay_->set_ui(console_.get());
        pal::logf(pal::LogLevel::Info, "overlay: dev console attached"); // overlay logs the resolved toggle key
    }
#endif
}

App::~App()
{
#ifdef MTHAP_HAS_OVERLAY
    overlay_.reset(); // removes render/input hooks + stops drawing first
    console_.reset(); // then unregister the log observer
#endif
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

#ifdef MTHAP_HAS_OVERLAY
void App::connect(const std::string &server, const std::string &slot, const std::string &password)
{
    link_->connect(server, slot, password);
}

void App::disconnect()
{
    link_->disconnect();
}

std::vector<std::string> App::status_lines() const
{
    std::vector<std::string> out;
    out.push_back(std::string("connected: ") + (link_->is_connected() ? "yes" : "no"));
    out.push_back("ap status: " + state_.status());
    out.push_back("player slot: " + std::to_string(state_.player_slot()));
    out.push_back("received items: " + std::to_string(state_.received_items().size()));
    return out;
}

std::vector<std::string> App::item_lines() const
{
    std::vector<std::string> out;
    for (const auto &it : state_.received_items())
        out.push_back("item_id=" + std::to_string(it.item_id) + " index=" + std::to_string(it.index) + " from=" + std::to_string(it.player_from));
    if (out.empty())
        out.push_back("(no items received yet)");
    return out;
}
#endif

} // namespace mth
