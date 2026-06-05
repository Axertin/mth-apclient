#include "mth/core/app.hpp"

#include <cstdlib>

#include "mth/core/ap_coordinator.hpp"
#include "mth/core/ap_link.hpp"
#include "mth/core/game_events.hpp"
#include "mth/core/inbound_granter.hpp"
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

// Thin forwarder - all tick logic lives in App so it owns all members. drive_tick runs
// after Game::FixedUpdate; grants drain just before World::Update, the spawn-safe window
// where the engine processes real pickups (so spawning item kinds don't hang the queue).
class AppTickSink final : public mth::IGameEvents
{
  public:
    explicit AppTickSink(mth::App &app) : app_(app)
    {
    }
    void on_game_fixed_update() override
    {
        app_.drive_tick();
    }
    void on_world_update_pre() override
    {
        app_.drain_grants();
    }

  private:
    mth::App &app_;
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
    events_ = std::make_unique<AppTickSink>(*this);
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

void App::drive_tick()
{
    if (!first_tick_logged_)
    {
        first_tick_logged_ = true;
        pal::logf(pal::LogLevel::Info, "tick: Game::FixedUpdate live; AP coordinator pumping");
    }
    coordinator_->tick();
    ensure_inbound_ready();
    if (inbound_)
        inbound_->tick();
}

void App::drain_grants()
{
    granter_.drain();
}

void App::ensure_inbound_ready()
{
    if (inbound_ || !state_.authenticated())
        return;
    const std::string key = "ap_" + state_.seed() + "_" + std::to_string(state_.player_slot()) + ".state";
    save_state_.emplace(pal::log_dir() / key);
    inbound_ = std::make_unique<InboundGranter>(granter_, state_, *save_state_);
    pal::logf(pal::LogLevel::Info, "inbound: state loaded (%s); granter live", key.c_str());
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

void App::give_item(std::int64_t ap_item_id)
{
    // Debug command: grants directly via the granter, independent of AP connection. The
    // only prerequisite is a cached Player* (observed on the first in-world pickup); report
    // clearly when that is not yet available rather than silently doing nothing.
    const int item_type = mth::game_item_type(ap_item_id);
    if (granter_.grant(item_type))
        pal::logf(pal::LogLevel::Info, "console: giveapitem %lld (type=%d) granted", static_cast<long long>(ap_item_id), item_type);
    else
        pal::logf(pal::LogLevel::Warn, "console: giveapitem %lld not granted yet (collect any in-world pickup/coin first to capture a player + position)",
                  static_cast<long long>(ap_item_id));
}
#endif

} // namespace mth
