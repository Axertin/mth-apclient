#include "mth/app/app.hpp"

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <vector>

#include "mod/mod_api.hpp"
#include "mth/app/ap_session.hpp"
#include "mth/app/gate_probes.hpp"
#include "mth/app/grant_pipeline.hpp"
#include "mth/app/hook_manager.hpp"
#include "mth/core/ap/ap_ids.hpp"
#include "mth/core/ap/ap_link.hpp"
#include "mth/core/data/ability_ids.hpp"
#include "mth/core/game_events.hpp"
#include "mth/core/rando_bridge.hpp"
#include "mth/features/player_tracker.hpp"
#include "mth/features/room_tracker.hpp"
#include "mth_version.h"
#include "pal/pal_game.hpp"
#include "pal/pal_hook.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"
#ifdef MTHAP_HAS_OVERLAY
#include "mth/core/data/game_symbols.hpp"
#include "mth/ui/overlay_root.hpp"
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
    // Every game-thread event is gated on App being fully constructed. The tick hooks go live mid-ctor
    // (GameHooks installs Game::FixedUpdate before hooks_ is assigned), so on a fast-initializing host an
    // event can arrive before App is wired; app_ exists (we are its member) but its members may not, and
    // ready() is the release/acquire barrier. Gating here covers every current and future forward at once.
    void on_game_fixed_update() override
    {
        if (app_.ready())
            app_.drive_tick();
    }
    void on_world_update_pre() override
    {
        if (!app_.ready())
            return;
        // Liveness first: this callback firing at all is the proof the native mod-hook path works,
        // and the gate's verdict depends on observing it.
        app_.gate_note_worldupdate();
        app_.drain_grants();
    }
    void on_world_destroy() override
    {
        if (app_.ready())
            app_.on_world_destroy();
    }

  private:
    mth::App &app_;
};

} // namespace

namespace mth
{

App::App() : login_prefs_(pal::log_dir() / "login.prefs")
{
    pal::logf(pal::LogLevel::Info, "mth-apclient %.*s loaded", static_cast<int>(version::string.size()), version::string.data());

    const auto game = pal::game_module();
    const auto self = pal::self_module();

    pal::logf(pal::LogLevel::Info, "game base=0x%llx size=0x%zx path=%s", static_cast<unsigned long long>(game.base), game.size, game.path.c_str());
    pal::logf(pal::LogLevel::Info, "self base=0x%llx path=%s", static_cast<unsigned long long>(self.base), self.path.c_str());

    // Must precede any mod-API call (game_revision() below included): usable() rejects every
    // entry as fail-open until the range is published.
    const pal::TextRange text = pal::game_text_range();
    pal::set_game_text_range(text);
    pal::logf(text.size != 0 ? pal::LogLevel::Info : pal::LogLevel::Warn, "pal: game .text range base=0x%llx size=0x%zx%s",
              static_cast<unsigned long long>(text.base), text.size, text.size != 0 ? "" : " (unresolved; mod-API pointer validation disabled)");

    const std::uint32_t game_rev = mod::game_revision();
    pal::logf(pal::LogLevel::Info, "game revision=r%u", game_rev);
    if (game_rev == 0)
        pal::logf(pal::LogLevel::Warn, "modding API reports revision 0: native mod hooks (WorldUpdate/IsItemCollected) will NOT fire on this "
                                       "build, so item grants and collection redirects are DISABLED. Ensure the game is the experimental-modding build.");

    pal::init_hook_engine();

    // AP safety gate: validate before anything touches game memory. Symbol resolution needs the
    // hook engine; the item-table probes must precede tables::repurpose_dummy_item(), which the
    // feature installers call later and which would otherwise have us validating our own write.
    gate_inputs_ = run_static_gate_probes(game_rev);
    // OBSERVE-ONLY for now: the verdict is computed, logged and shown, but does not block AP
    // behavior until it is switched on with `gate enforce on`. A wrong probe must not brick the
    // mod before the probes have been confirmed against real game builds.
    gate_tick();
    pal::logf(pal::LogLevel::Info, "gate: verdict=%s enforcing=0 (console: `gate enforce on`)", verdict_name(gate_verdict_.load()));

    net_ = std::make_unique<ApSession>(
        state_, [this] { pending_inbound_death_.store(true); },
        // on_scout: fill the registry on the game thread (coordinator tick runs there).
        [this](const std::vector<mth::ScoutInfo> &locs)
        {
            for (const auto &s : locs)
                scout_registry_.record(s);
        },
        // on_session_reset: fires on the game thread (coordinator tick) for a fresh ApConnected or
        // ApDisconnected. The registry is lock-free game-thread-only data; disconnect() itself runs on
        // the overlay render thread (ICommandSink), so clearing it there would race a concurrent
        // lookup()/record() here. Also covers connect-to-a-new-server-without-disconnecting, which would
        // otherwise leave stale entries under reused slot numbers.
        [this]
        {
            // Must precede anything that writes the live SaveSlot this tick (lock seeding, the upgrade
            // re-apply, the takeover), which the coordinator drain does. The game's save path is also
            // what uploads to Steam Cloud (ycUserProfile::WriteSaveFile makes the RemoteStorage call),
            // so a leaked write reaches the player's cloud save. Off on disconnect too, so no stale
            // mod-written slot can persist.
            mod::set_save_write_enabled(false);
            pal::logf(pal::LogLevel::Info, "save: vanilla save writes suppressed (save api=%s, enabled=%d)", mod::save_api_available() ? "ok" : "MISSING",
                      mod::save_write_enabled() ? 1 : 0);
            scout_registry_.clear();
        },
        // on_session_end: a different (seed, slot) authenticated. Ordered ahead of that connection's
        // ApConnected, so it is the one safe point to drop the previous session wholesale.
        [this] { clear_session_state(); });
    tracker_ = std::make_unique<PlayerTracker>();
    room_tracker_ = std::make_unique<RoomTracker>();
    events_ = std::make_unique<AppTickSink>(*this);
    grants_ = std::make_unique<GrantPipeline>(
        *tracker_, [this](int loc) { return net_->rando().is_ap_location(loc); }, [this](int loc) { net_->rando().on_location_collected(loc); });
    // Suppress the game's default new-file starting kit while AP-authenticated (AP supplies it instead).
    // SaveSlot::Clear also fires on profile-menu / save-load, so the zero can hit an existing save's upgrade
    // fields; re-arm the upgrade re-apply each time we suppress so drive_tick refills them from AP state.
    pal::install_newfile_kit_suppressor(
        [this]
        {
            if (!state_.authenticated())
                return false;
            upgrades_.force_dirty();
            return true;
        });

    // Built last: GameHooks needs *events_, and the manager's hooks tick into all managers.
    hooks_ = std::make_unique<HookManager>(
        *events_, net_->rando(), scout_registry_, state_, [this] { net_->link().send_death("Mina the Hollower"); },
        [this]() -> void * { return tracker_->player(); });
#ifdef MTHAP_HAS_OVERLAY
    {
        const pal::OverlayConfig ocfg{pal::resolve_game_symbol(sym::process_sdl_event)};
        overlay_root_ = std::make_unique<OverlayRoot>(*this, net_->banner_queue());
        overlay_ = pal::make_overlay(ocfg);
        overlay_->set_ui(overlay_root_.get());
        pal::logf(pal::LogLevel::Info, "overlay: dev console attached"); // overlay logs the resolved toggle key
    }
#endif
    ready_.store(true); // everything wired: allow the game-thread tick hooks to run (they may already be firing)
}

App::~App()
{
    ready_.store(false); // stop ticking before any member is torn down
#ifdef MTHAP_HAS_OVERLAY
    overlay_.reset();      // removes render/input hooks + stops drawing first
    overlay_root_.reset(); // then unregister the log observer
#endif
    pal::remove_newfile_kit_suppressor();
    hooks_.reset(); // GameHooks (tick source) stops first inside the manager, then feature hooks
    grants_.reset();
    room_tracker_.reset();
    tracker_.reset();
    events_.reset(); // AppTickSink; must outlive GameHooks (now gone) - reset after hooks_
    net_.reset();    // link stops the net thread last
    pal::shutdown_hook_engine();
    pal::logf(pal::LogLevel::Info, "mth-apclient unloading");
}

void App::run()
{
    pal::logf(pal::LogLevel::Info, "App::run: tick hooks installed, idling");
}

void App::drive_tick()
{
    if (!gate_latch_.settled())
    {
        ++gate_inputs_.ticks_since_probe_installed;
        gate_tick();
    }
    if (!first_tick_logged_)
    {
        first_tick_logged_ = true;
        pal::logf(pal::LogLevel::Info, "tick: Game::FixedUpdate live; AP coordinator pumping");
    }
    std::optional<std::uint32_t> screen;
    std::uint32_t screen_id = 0;
    if (room_tracker_ && room_tracker_->current_screen(&screen_id))
        screen = screen_id;
    net_->tick(state_, screen);
    hooks_->tick(state_, policy_, save_state_ ? save_state_->game_slot() : -1);
    upgrades_.recompute(state_);
    // Diagnostic (#46): apply_upgrades runs only while dirty, so if it never fires the pal-layer trace is
    // silent. Log the call-site gating on change to separate "never attempted" from "attempted and failed".
    {
        static int s_last_gate = -1;
        const int gate = !upgrades_.dirty() ? 0 : (!tracker_ ? 1 : (tracker_->player() == nullptr ? 2 : 3));
        if (gate != s_last_gate)
        {
            s_last_gate = gate;
            const int *c = upgrades_.counts();
            const char *w = gate == 0 ? "idle (not dirty)" : gate == 1 ? "dirty, no tracker" : gate == 2 ? "dirty, player null" : "dirty, attempting apply";
            pal::logf(pal::LogLevel::Debug, "upgrades: drive_tick gate -> %s counts=[%d,%d,%d,%d,%d]", w, c[0], c[1], c[2], c[3], c[4]);
        }
    }
    if (upgrades_.dirty() && tracker_ && pal::apply_upgrades(upgrades_.counts(), tracker_->player()))
    {
        apply_vial_capacity();    // vials go through the offset-free mod API, not a raw SaveSlot poke
        upgrades_.mark_applied(); // applied to the save; retry next tick if player not ready yet
    }
    enforce_wallet_cap();
    if (pending_inbound_death_.exchange(false))
        hooks_->kill_player();
    ensure_inbound_ready();
    reconcile_server_checked();
    if (resend_gate_.fire(net_->link().is_connected(), grants_->inbound_ready()))
    {
        net_->rando().flush(); // (re)connect: resend the full persisted checked set; server dedups
        pal::logf(pal::LogLevel::Info, "outbound: (re)connect -> flushed checked-set");
    }
    grants_->tick();
    // Persist a freshly captured AP-game slot so it's known on the next load/session.
    if (save_state_)
    {
        const int s = hooks_->captured_ap_slot();
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
    hooks_->drain();
    grants_->drain();
}

void App::apply_vial_capacity()
{
    // Vial capacity is popcount of a SaveSlot bitfield whose offset drifts between builds (#97); drive it
    // through the mod API instead, which resolves the current player/save-slot itself. Preserve the missing
    // flask count across the capacity change. No-op until a player exists.
    if (!mod::vial_api_available())
        return;
    const int want = upgrades_.counts()[kVialUpgradeIndex];
    const int old_max = mod::player_max_vials();
    const int old_held = mod::player_vials();
    mod::set_player_max_vials(want);
    mod::set_player_vials(maintained_vial_held(old_max, old_held, want));
}

void App::enforce_wallet_cap()
{
    // slot_data "wallet_cap": cap the bone wallet at 750 + 500 per received wallet item, uncapped at 8+.
    // The game's own cap field drifts between builds, so clamp current bones through the offset-free mod API
    // each frame instead. Inert unless AP-authenticated and the flag is set; vanilla play is untouched.
    if (!state_.authenticated() || !state_.wallet_cap())
        return;
    wallet_.recompute(state_);
    const std::optional<int> cap = wallet_.enforced_cap();
    if (!cap || !mod::bones_api_available())
        return;
    if (mod::player_bones() > *cap)
        mod::set_player_bones(*cap);
}

void App::gate_note_worldupdate()
{
    if (gate_inputs_.worldupdate_observed)
        return;
    gate_inputs_.worldupdate_observed = true;
    pal::logf(pal::LogLevel::Info, "gate: WorldUpdate observed after %d tick(s); native mod hooks are live", gate_inputs_.ticks_since_probe_installed);
    gate_tick();
}

void App::gate_tick()
{
    const GateVerdict before = gate_latch_.verdict();
    const GateVerdict after = gate_latch_.update(gate_inputs_);
    if (after == before)
        return;

    gate_verdict_.store(after);
    std::string reason = refusal_reason(gate_inputs_);
    if (after == GateVerdict::Refused)
    {
        pal::logf(pal::LogLevel::Error, "gate: REFUSED - %s", reason.c_str());
        if (!gate_enforcing_.load())
            pal::logf(pal::LogLevel::Warn, "gate: observe-only, so AP behavior is NOT being blocked; run `gate enforce on` to enforce");
    }
    else if (after == GateVerdict::Clear)
    {
        pal::logf(pal::LogLevel::Info, "gate: CLEAR after %d tick(s); AP behavior permitted", gate_inputs_.ticks_since_probe_installed);
        if (!gate_inputs_.revision_known)
            pal::logf(pal::LogLevel::Warn, "gate: this game build is outside the validated revision range; proceeding because every probe passed");
        if (!gate_inputs_.mod_api_shape_ok)
            pal::logf(pal::LogLevel::Error, "gate: the game's mod API version differs from the one this build expects; entries may be misaligned");
    }
    {
        std::lock_guard<std::mutex> lk(gate_reason_mutex_);
        gate_reason_ = std::move(reason);
    }
}

void App::on_world_destroy()
{
    // The Player is freed with the world; drop our cached pointer so the next drive_tick's upgrade
    // re-apply (armed by the newfile-kit suppressor on reload) can't write through a dead Player.
    if (tracker_)
        tracker_->invalidate_player();
    // A save reload clears s_rItemCollection of our in-memory collected-bit writes; re-arm so the next
    // in-game tick re-applies them for server-collected durable-bit locations.
    if (hooks_)
        hooks_->on_world_destroy();
}

// A different (seed, slot) authenticated. The mod owns one save per (seed, slot), so a clean slate is the
// answer rather than reconciling two servers' state. The driving ApSessionEnded lands ahead of the new
// ApConnected, so that connection's items and checked-location report apply on top, never under. A
// reconnect to the same seed+slot never reaches here: that session is still live, and tearing it down
// would restart the run (#152).
void App::clear_session_state()
{
    net_->rando().reset_session(); // releases the save; must precede save_state_.reset()
    grants_->release_inbound();    // holds an ApSaveState reference; same ordering constraint
    save_state_.reset();
    state_.reset_session();
    scout_registry_.clear();
    hooks_->clear_session_state();
    // Must reset alongside the received stream: UpgradeState::applied_ goes back to zero with it, so the
    // next recompute is not dirty and cannot write a downgrade into the live save.
    upgrades_ = UpgradeState{};
    wallet_ = WalletCapState{};
    resend_gate_ = ConnectResendGate{}; // re-arm so the next connection flushes its own checked-set
    pending_inbound_death_.store(false);
    pal::logf(pal::LogLevel::Info, "session: cleared previous AP connection state");
}

void App::ensure_inbound_ready()
{
    if (grants_->inbound_ready() || !state_.authenticated())
        return;
    remember_successful_login(); // first tick after authentication: this target worked
    const std::string key = "ap_" + state_.seed() + "_" + std::to_string(state_.player_slot()) + ".state";
    save_state_.emplace(pal::log_dir() / key);
    grants_->build_inbound(state_, *save_state_, [this] { return hooks_->credit_kear_key(); }); // vanilla-kear key grant (#130)
    pal::logf(pal::LogLevel::Info, "inbound: state loaded (%s); granter live", key.c_str());
    hooks_->set_ap_slot(save_state_->game_slot()); // restore the AP-game slot (skip capture if known)
    net_->rando().attach_save_state(*save_state_);
    pal::logf(pal::LogLevel::Info, "outbound: bridge attached to %s", key.c_str());
}

void App::reconcile_server_checked()
{
    if (!save_state_) // inbound not ready yet; ids remain pending in ApState until the save attaches
        return;
    const auto ids = state_.take_server_checked_pending();
    bool changed = false;
    for (std::int64_t id : ids)
        if (id >= 0 && id <= std::numeric_limits<int>::max())
            changed |= net_->rando().reconcile_server_checked(static_cast<int>(id));
    if (changed)
        save_state_->save(); // one persist per reconcile pass (batched)
}

void App::connect(const std::string &server, const std::string &slot, const std::string &password)
{
    if (gate_enforcing_.load() && gate_verdict_.load() == GateVerdict::Refused)
    {
        std::string reason;
        {
            std::lock_guard<std::mutex> lk(gate_reason_mutex_);
            reason = gate_reason_;
        }
        // Deliberately does not touch ApState: connect() runs on the overlay render thread, and
        // ApState's status/detail are game-thread data. The UI reads the reason via gate_status().
        pal::logf(pal::LogLevel::Error, "gate: refusing connect - %s", reason.c_str());
        return;
    }
    {
        std::lock_guard<std::mutex> lk(login_mutex_);
        pending_server_ = server;
        pending_slot_ = slot;
    }
    net_->link().connect(server, slot, password);
}

SavedLogin App::saved_login() const
{
    std::lock_guard<std::mutex> lk(login_mutex_);
    return SavedLogin{login_prefs_.server(), login_prefs_.slot()};
}

// Commit the attempted target only once the server has authenticated, so a typo or a dead
// host never overwrites a known-good one.
void App::remember_successful_login()
{
    std::lock_guard<std::mutex> lk(login_mutex_);
    if (pending_server_.empty())
        return;
    if (pending_server_ == login_prefs_.server() && pending_slot_ == login_prefs_.slot())
        return;
    login_prefs_.set(pending_server_, pending_slot_);
    login_prefs_.save();
    pal::logf(pal::LogLevel::Info, "login: remembered %s / %s", pending_server_.c_str(), pending_slot_.c_str());
}

void App::disconnect()
{
    // Runs on the overlay render thread (ICommandSink caller), not the game thread. The registry is
    // cleared from the coordinator's on_session_reset callback instead (game thread; see ctor), since
    // clearing the lock-free map here would race a concurrent game-thread lookup()/record().
    net_->link().disconnect();
}

ConnectionStatus App::connection_status() const
{
    return ConnectionStatus{state_.phase(), state_.detail()};
}

std::vector<std::string> App::status_lines() const
{
    std::vector<std::string> out;
    out.push_back(std::string("connected: ") + (net_->link().is_connected() ? "yes" : "no"));
    out.push_back("ap status: " + state_.status());
    out.push_back("player slot: " + std::to_string(state_.player_slot()));
    out.push_back("received items: " + std::to_string(state_.received_items().size()));
    hooks_->append_status_lines(out);
    return out;
}

GateStatus App::gate_status() const
{
    GateStatus s;
    s.verdict = gate_verdict_.load();
    s.enforcing = gate_enforcing_.load();
    {
        std::lock_guard<std::mutex> lk(gate_reason_mutex_);
        s.reason = gate_reason_;
    }
    return s;
}

void App::set_gate_enforcing(bool on)
{
    gate_enforcing_.store(on);
    pal::logf(pal::LogLevel::Info, "gate: enforcing=%d (verdict=%s)", on ? 1 : 0, verdict_name(gate_verdict_.load()));
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
    // Non-vanilla ids and capacity upgrades are applied by a received-item handler, not the granter;
    // inject into the list the socket feeds so the dev path matches a real receipt.
    if (!is_vanilla_game_item(ap_item_id) || is_capacity_upgrade_item(ap_item_id))
    {
        state_.inject_received_item(ap_item_id);
        pal::logf(pal::LogLevel::Info, "console: giveapitem %lld -> injected as received item (applied by its segment handler)",
                  static_cast<long long>(ap_item_id));
        return;
    }

    const int item_type = mth::game_item_type(ap_item_id);
    if (grants_->grant(item_type))
        pal::logf(pal::LogLevel::Info, "console: giveapitem %lld (type=%d) granted", static_cast<long long>(ap_item_id), item_type);
    else
        pal::logf(pal::LogLevel::Warn, "console: giveapitem %lld not ready (collect any pickup first to capture player + position)",
                  static_cast<long long>(ap_item_id));
}

void App::remove_lock(int slot)
{
    hooks_->remove_lock(slot);
    pal::logf(pal::LogLevel::Info, "console: removelock %d (live if spawned; opens on entry otherwise)", slot);
}

void App::set_modifier(int idx, bool on)
{
    policy_.arm_console_modifiers();
    hooks_->set_modifier_live(idx, on);
    pal::logf(pal::LogLevel::Info, "console: modifier %d %s", idx, on ? "on" : "off");
}

void App::lock_modifiers(bool armed)
{
    policy_.arm_console_modifiers();
    hooks_->set_modifiers_armed(armed);
    pal::logf(pal::LogLevel::Info, "console: modifiers %s", armed ? "locked" : "unlocked");
}

void App::set_stat_caps(int attack, int defense, int sidearm)
{
    policy_.arm_forced_caps();
    hooks_->set_stat_caps(attack, defense, sidearm);
    pal::logf(pal::LogLevel::Info, "console: stat caps attack=%d defense=%d sidearm=%d", attack, defense, sidearm);
}

void App::set_ability_randomized(Ability a, bool on)
{
    policy_.arm_console_abilities();
    hooks_->set_ability_randomized(a, on);
    pal::logf(pal::LogLevel::Info, "console: ability %d randomized %s", static_cast<int>(a), on ? "on" : "off");
}

void App::enable_deathlink(bool on)
{
    net_->link().enable_deathlink(on);
    pal::logf(pal::LogLevel::Info, "console: deathlink %s", on ? "enabled" : "disabled");
}

void App::set_lit_lamps(std::uint32_t lamp_mask)
{
    hooks_->set_lamp_console_override(lamp_mask);
    pal::logf(pal::LogLevel::Info, "console: fountain lamp override mask=0x%x", lamp_mask);
}

void App::save_test(const std::string &op)
{
    if (!mod::save_api_available())
    {
        pal::logf(pal::LogLevel::Warn, "savetest: modding save API unavailable (revision=%u)", mod::game_revision());
        return;
    }

    if (op == "dump")
    {
        const std::string blob = mod::active_save_slot_contents();
        pal::logf(pal::LogLevel::Info, "savetest: slot=%d bytes=%zu head=%.32s", mod::active_save_slot(), blob.size(), blob.c_str());
    }
    else if (op == "write")
    {
        // Capture then restore the identical blob: a session that survives this proves the
        // serialize/deserialize pair is lossless enough to build save handling on.
        const std::string blob = mod::active_save_slot_contents();
        if (blob.empty())
        {
            // active_save_slot_contents() returns "" on every failure path; forwarding that as a payload
            // would overwrite the live save slot with nothing, so bail before touching it.
            pal::logf(pal::LogLevel::Warn, "savetest: capture returned empty blob; skipping write to avoid clobbering the active save slot");
            return;
        }
        const bool ok = mod::set_active_save_slot_contents(blob.c_str());
        mod::player_restore_from_save();
        pal::logf(pal::LogLevel::Info, "savetest: round-trip set=%s bytes=%zu", ok ? "ok" : "FAILED", blob.size());
    }
    else if (op == "noflush")
    {
        // With writes suppressed the game must stop touching saveData.yc; confirm via its mtime.
        mod::set_save_write_enabled(false);
        pal::logf(pal::LogLevel::Info, "savetest: save writes disabled (enabled=%d)", mod::save_write_enabled() ? 1 : 0);
    }
    else if (op == "flush")
    {
        // Re-opens the one path by which mod state can reach vanilla saveData.yc (and Steam Cloud);
        // refused during a takeover.
        if (hooks_->takeover_active())
        {
            pal::logf(pal::LogLevel::Warn, "savetest: refusing to enable save writes; a save takeover is active");
            return;
        }
        mod::set_save_write_enabled(true);
        pal::logf(pal::LogLevel::Info, "savetest: save writes enabled (enabled=%d)", mod::save_write_enabled() ? 1 : 0);
    }
    else
    {
        pal::logf(pal::LogLevel::Warn, "savetest: unknown op '%s'", op.c_str());
    }
}
} // namespace mth
