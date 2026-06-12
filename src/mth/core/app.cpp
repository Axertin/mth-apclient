#include "mth/core/app.hpp"

#include <cstdio>
#include <cstdlib>

#include "mth/core/ap_coordinator.hpp"
#include "mth/core/ap_ids.hpp"
#include "mth/core/ap_link.hpp"
#include "mth/core/game_events.hpp"
#include "mth/core/inbound_granter.hpp"
#include "mth/core/modifier_config.hpp"
#include "mth/core/rando_bridge.hpp"
#include "mth/hooks/death_hooks.hpp"
#include "mth/hooks/game_hooks.hpp"
#include "mth/hooks/item_granter.hpp"
#include "mth/hooks/player_tracker.hpp"
#include "mth/hooks/rando_hooks.hpp"
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

// Thin forwarder to App. drive_tick: post-FixedUpdate. drain_grants: pre-World::Update spawn window.
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
    pal::logf(pal::LogLevel::Warn, "net: lane NOT compiled in (NullApLink); connect is a no-op");
#endif
    coordinator_ = std::make_unique<ApCoordinator>(*link_, state_, [this] { pending_inbound_death_.store(true); });
    events_ = std::make_unique<AppTickSink>(*this);
    hooks_ = std::make_unique<GameHooks>(*events_);
    tracker_ = std::make_unique<PlayerTracker>();
    granter_ = std::make_unique<ItemGranter>(*tracker_);
    rando_ = std::make_unique<RandoBridge>(*link_, state_);
    rando_hooks_ = std::make_unique<RandoHooks>(*rando_);
    death_hooks_ = std::make_unique<DeathHooks>([this] { link_->send_death("Mina the Hollower"); }, [this]() -> void * { return tracker_->player(); });

    if (const char *locks = std::getenv("MTHAP_REMOVE_LOCKS"); locks && *locks)
    {
        rando_hooks_->locks().add_from_list(locks);
        pal::logf(pal::LogLevel::Info, "locks: removed-set seeded from MTHAP_REMOVE_LOCKS=%s", locks);
    }

    {
        mth::ModifierRequest mreq;
        if (const char *m = std::getenv("MTHAP_MODIFIERS"); m && *m)
        {
            mreq = mth::parse_modifier_indices(m);
            modifiers_from_env_ = true; // offline test mode: enforce without an AP connection
            pal::logf(pal::LogLevel::Info, "modifiers: %zu index(es) from MTHAP_MODIFIERS (offline test mode)", mreq.indices.size());
        }
        modifier_hooks_ = std::make_unique<ModifierHooks>(std::move(mreq));
    }

    level_cap_hooks_ = std::make_unique<LevelCapHooks>();
    if (const char *sc = std::getenv("MTHAP_STAT_CAPS"); sc && *sc)
    {
        int a = 0, b = 0, c = 0;
        if (std::sscanf(sc, "%d,%d,%d", &a, &b, &c) == 3)
        {
            level_cap_hooks_->set_counts(a, b, c);
            caps_forced_ = true;
            pal::logf(pal::LogLevel::Info, "levelcap: forced caps attack=%d defense=%d sidearm=%d (offline test)", a, b, c);
        }
        else
        {
            pal::logf(pal::LogLevel::Warn, "levelcap: MTHAP_STAT_CAPS=%s malformed (want a,b,c); ignored", sc);
        }
    }

    // MTHAP_MOCK_AP: offline test mode; fakes AP-connected state for locations 0..N (default N=1024).
    if (const char *mock = std::getenv("MTHAP_MOCK_AP"); mock && *mock)
    {
        int max_idx = std::atoi(mock);
        if (max_idx < 2)
            max_idx = 1024;
        ApConnected ev;
        ev.seed = "mock";
        ev.player_slot = 0;
        for (int i = 0; i <= max_idx; ++i)
            ev.missing_locations.push_back(ap_loc_id(i));
        state_.apply(ev);
        pal::logf(pal::LogLevel::Info, "AP: MOCK state injected (every pickup is an AP location, idx 0..%d)", max_idx);
    }

    if (const char *dl = std::getenv("MTHAP_DEATHLINK"); dl && *dl && std::atoi(dl) != 0)
    {
        link_->enable_deathlink(true);
        pal::logf(pal::LogLevel::Info, "deathlink: enabled (MTHAP_DEATHLINK)");
    }

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
    death_hooks_.reset();
    modifier_hooks_.reset();
    level_cap_hooks_.reset();
    rando_hooks_.reset();
    rando_.reset();
    granter_.reset();
    tracker_.reset();
    hooks_.reset();
    events_.reset();
    coordinator_.reset();
    link_.reset();
    pal::shutdown_hook_engine();
    pal::logf(pal::LogLevel::Info, "mth-apclient unloading");
}

void App::run()
{
    pal::logf(pal::LogLevel::Info, "App::run: tick hooks installed, idling");
}

void App::drive_tick()
{
    if (!first_tick_logged_)
    {
        first_tick_logged_ = true;
        pal::logf(pal::LogLevel::Info, "tick: Game::FixedUpdate live; AP coordinator pumping");
    }
    coordinator_->tick();
    if (modifier_hooks_)
    {
        // Enforce (seed + lockdown) only in an AP session, offline test mode, or once the console
        // drove modifiers; ap_scoped (authed only) restricts the seed to the captured AP-game slot.
        const bool authed = state_.authenticated();
        modifier_hooks_->set_enforce_live(authed || modifiers_from_env_ || modifiers_console_active_);
        modifier_hooks_->set_ap_scoped(authed);
        modifier_hooks_->drain_live();
    }
    if (level_cap_hooks_)
    {
        level_cap_hooks_->set_enforce_live(state_.authenticated() || caps_forced_);
        if (!caps_forced_)
            level_cap_hooks_->recompute(state_);
    }
    if (pending_inbound_death_.exchange(false) && death_hooks_)
        death_hooks_->kill();
    ensure_inbound_ready();
    if (inbound_)
        inbound_->tick();
    // Persist a freshly captured AP-game slot so it's known on the next load/session.
    if (modifier_hooks_ && save_state_)
    {
        const int s = modifier_hooks_->captured_ap_slot();
        if (s >= 0 && s != save_state_->game_slot())
        {
            save_state_->set_game_slot(s);
            save_state_->save();
            pal::logf(pal::LogLevel::Info, "modifiers: persisted AP-game slot %d", s);
        }
    }
}

void App::drain_grants()
{
    if (rando_hooks_)
        rando_hooks_->seed_removed_locks();
    granter_->drain();
}

void App::ensure_inbound_ready()
{
    if (inbound_ || !state_.authenticated())
        return;
    const std::string key = "ap_" + state_.seed() + "_" + std::to_string(state_.player_slot()) + ".state";
    save_state_.emplace(pal::log_dir() / key);
    inbound_ = std::make_unique<InboundGranter>(*granter_, state_, *save_state_);
    pal::logf(pal::LogLevel::Info, "inbound: state loaded (%s); granter live", key.c_str());
    if (modifier_hooks_)
        modifier_hooks_->set_ap_slot(save_state_->game_slot()); // restore the AP-game slot (skip capture if known)
    rando_->attach_save_state(*save_state_);
    rando_->flush(); // resend any checks recorded before/while disconnected
    pal::logf(pal::LogLevel::Info, "outbound: bridge attached to %s; flushed checked-set", key.c_str());
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
    if (modifier_hooks_)
        for (const auto &l : modifier_hooks_->status_lines())
            out.push_back(l);
    if (level_cap_hooks_)
        for (const auto &l : level_cap_hooks_->status_lines())
            out.push_back(l);
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
    const int item_type = mth::game_item_type(ap_item_id);
    if (granter_->grant(item_type))
        pal::logf(pal::LogLevel::Info, "console: giveapitem %lld (type=%d) granted", static_cast<long long>(ap_item_id), item_type);
    else
        pal::logf(pal::LogLevel::Warn, "console: giveapitem %lld not ready (collect any pickup first to capture player + position)",
                  static_cast<long long>(ap_item_id));
}

void App::remove_lock(int slot)
{
    rando_hooks_->locks().set_removed(slot);
    pal::logf(pal::LogLevel::Info, "console: removelock %d (live if spawned; opens on entry otherwise)", slot);
}

void App::set_modifier(int idx, bool on)
{
    modifiers_console_active_ = true; // dev is driving modifiers -> enforcement (incl. lockdown) is live
    if (modifier_hooks_)
        modifier_hooks_->set_live(idx, on);
    pal::logf(pal::LogLevel::Info, "console: modifier %d %s", idx, on ? "on" : "off");
}

void App::lock_modifiers(bool armed)
{
    modifiers_console_active_ = true; // explicit console lock/unlock -> enforcement is live this session
    if (modifier_hooks_)
        modifier_hooks_->set_armed(armed);
    pal::logf(pal::LogLevel::Info, "console: modifiers %s", armed ? "locked" : "unlocked");
}

void App::set_stat_caps(int attack, int defense, int sidearm)
{
    caps_forced_ = true; // console drove caps -> enforce this session (like MTHAP_STAT_CAPS); skips AP recompute
    if (level_cap_hooks_)
        level_cap_hooks_->set_counts(attack, defense, sidearm);
    pal::logf(pal::LogLevel::Info, "console: stat caps attack=%d defense=%d sidearm=%d", attack, defense, sidearm);
}
#endif

} // namespace mth
