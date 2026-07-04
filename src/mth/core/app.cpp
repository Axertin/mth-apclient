#include "mth/core/app.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "mod/mod_api.hpp"
#include "mth/core/ap_coordinator.hpp"
#include "mth/core/ap_ids.hpp"
#include "mth/core/ap_link.hpp"
#include "mth/core/area_reporter.hpp"
#include "mth/core/broadcast.hpp"
#include "mth/core/game_events.hpp"
#include "mth/core/inbound_granter.hpp"
#include "mth/core/modifier_config.hpp"
#include "mth/core/rando_bridge.hpp"
#include "mth/features/ability_hooks.hpp"
#include "mth/features/boss_hooks.hpp"
#include "mth/features/chest_hooks.hpp"
#include "mth/features/death_hooks.hpp"
#include "mth/features/goal_tracker.hpp"
#include "mth/features/item_granter.hpp"
#include "mth/features/location_hooks.hpp"
#include "mth/features/lock_hooks.hpp"
#include "mth/features/pawn_shop_hooks.hpp"
#include "mth/features/player_tracker.hpp"
#include "mth/features/room_tracker.hpp"
#include "mth/hooks/game_hooks.hpp"
#include "mth_version.h"
#include "pal/pal_game.hpp"
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

    pal::logf(pal::LogLevel::Info, "game base=0x%llx size=0x%zx path=%s", static_cast<unsigned long long>(game.base), game.size, game.path.c_str());
    pal::logf(pal::LogLevel::Info, "self base=0x%llx path=%s", static_cast<unsigned long long>(self.base), self.path.c_str());
    const std::uint32_t game_rev = mod::game_revision();
    pal::logf(pal::LogLevel::Info, "game revision=r%u", game_rev);
    if (game_rev == 0)
        pal::logf(pal::LogLevel::Warn, "modding API reports revision 0: native mod hooks (WorldUpdate/IsItemCollected) will NOT fire on this "
                                       "build, so item grants and collection redirects are DISABLED. Ensure the game is the experimental-modding build.");

    pal::init_hook_engine();

#ifdef MTHAP_HAS_NET
    link_ = std::make_unique<mth::net::ApLink>();
#else
    link_ = std::make_unique<mth::NullApLink>();
    pal::logf(pal::LogLevel::Warn, "net: lane NOT compiled in (NullApLink); connect is a no-op");
#endif
    std::function<void(const std::vector<mth::BannerSegment> &)> on_broadcast;
#ifdef MTHAP_HAS_OVERLAY
    banner_queue_ = std::make_unique<BannerQueue>();
    on_broadcast = [this](const std::vector<mth::BannerSegment> &segs) { banner_queue_->push(segs); };
#endif
    coordinator_ = std::make_unique<ApCoordinator>(*link_, state_, [this] { pending_inbound_death_.store(true); }, std::move(on_broadcast));
    area_reporter_ = std::make_unique<AreaReporter>(*link_);
    events_ = std::make_unique<AppTickSink>(*this);
    hooks_ = std::make_unique<GameHooks>(*events_);
    tracker_ = std::make_unique<PlayerTracker>();
    room_tracker_ = std::make_unique<RoomTracker>();
    granter_ = std::make_unique<ItemGranter>(
        *tracker_, [this](int loc) { return rando_ != nullptr && rando_->is_ap_location(loc); },
        [this](int loc)
        {
            if (rando_ != nullptr)
                rando_->on_location_collected(loc);
        });
    rando_ = std::make_unique<RandoBridge>(*link_, state_);
    location_hooks_ = std::make_unique<LocationHooks>(*rando_);
    boss_hooks_ = std::make_unique<BossHooks>(*rando_);
    goal_tracker_ = std::make_unique<GoalTracker>(*rando_);
    lock_hooks_ = std::make_unique<LockHooks>();
    chest_hooks_ = std::make_unique<ChestHooks>(lock_hooks_->locks()); // shares the lock registry + seed
    death_hooks_ = std::make_unique<DeathHooks>([this] { link_->send_death("Mina the Hollower"); }, [this]() -> void * { return tracker_->player(); });
    ability_hooks_ = std::make_unique<AbilityHooks>([this](std::int64_t id) { return state_.has_received(id); });
    pawn_shop_hooks_ = std::make_unique<PawnShopHooks>([this] { return state_.phase() == ConnectionPhase::Connected; });
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

    modifier_hooks_ = std::make_unique<ModifierHooks>(ModifierRequest{});

    level_cap_hooks_ = std::make_unique<LevelCapHooks>();
#ifdef MTHAP_HAS_OVERLAY
    {
        const pal::OverlayConfig ocfg{pal::resolve_game_symbol(sym::process_sdl_event)};
        overlay_root_ = std::make_unique<OverlayRoot>(*this, *banner_queue_);
        overlay_ = pal::make_overlay(ocfg);
        overlay_->set_ui(overlay_root_.get());
        pal::logf(pal::LogLevel::Info, "overlay: dev console attached"); // overlay logs the resolved toggle key
    }
#endif
}

App::~App()
{
#ifdef MTHAP_HAS_OVERLAY
    overlay_.reset();      // removes render/input hooks + stops drawing first
    overlay_root_.reset(); // then unregister the log observer
#endif
    pal::remove_newfile_kit_suppressor();
    ability_hooks_.reset();
    pawn_shop_hooks_.reset();
    death_hooks_.reset();
    modifier_hooks_.reset();
    level_cap_hooks_.reset();
    goal_tracker_.reset();
    location_hooks_.reset();
    boss_hooks_.reset();
    chest_hooks_.reset(); // references lock_hooks_'s registry; tear down first
    lock_hooks_.reset();
    rando_.reset();
    granter_.reset();
    room_tracker_.reset();
    tracker_.reset();
    hooks_.reset();
    events_.reset();
    area_reporter_.reset();
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
    if (area_reporter_ && room_tracker_)
    {
        std::uint32_t screen = 0;
        const bool have = room_tracker_->current_screen(&screen);
        area_reporter_->tick(state_.authenticated(), have ? std::optional<std::uint32_t>{screen} : std::nullopt);
    }
    const bool authed = state_.authenticated();
    if (modifier_hooks_)
    {
        // Enforce (seed + lockdown) only in an AP session, offline test mode, or once the console
        // drove modifiers; ap_scoped (authed only) restricts the seed to the captured AP-game slot.
        modifier_hooks_->set_enforce_live(policy_.enforce_modifiers(authed));
        modifier_hooks_->set_ap_scoped(authed);
        modifier_hooks_->set_ossex_start(state_.ossex_start()); // force Landing Done when slot_data requests it
        modifier_hooks_->drain_live();
    }
    if (level_cap_hooks_)
    {
        level_cap_hooks_->set_enforce_live(policy_.enforce_caps(authed));
        if (!policy_.caps_fixed())
            level_cap_hooks_->recompute(state_);
    }
    if (location_hooks_)
    {
        location_hooks_->set_kear_rando(state_.kear_rando()); // slot_data flag: neutralize the world-kear key grant
        location_hooks_->reconcile_kear_keys();               // re-cancel AP kears that a reload restored as usable keys
    }
    if (goal_tracker_ && authed)
        goal_tracker_->evaluate(state_); // poll SaveSlot; fires the AP goal when the slot_data condition is met
    if (ability_hooks_)
    {
        if (authed)
        {
            ability_hooks_->set_randomized(Ability::Burrow, state_.burrow_rando());
            ability_hooks_->set_randomized(Ability::Swim, state_.swim_rando());
            ability_hooks_->set_randomized(Ability::RopeClimb, state_.rope_rando());
            ability_hooks_->set_randomized(Ability::BouncePuff, state_.puff_rando());
            ability_hooks_->set_randomized(Ability::BounceSpring, state_.spring_rando());
            ability_hooks_->set_randomized(Ability::Carry, state_.carry_rando());
            ability_hooks_->set_randomized(Ability::Train, state_.train_rando());
        }
        // offline: leave randomized as the `ability` console command set it.
        const bool armed = policy_.enforce_abilities(authed);
        const bool slot_ok = !authed ? true // offline console test: enforce on the loaded save
                                     : (save_state_.has_value() && save_state_->game_slot() >= 0 && modifier_hooks_ &&
                                        modifier_hooks_->captured_ap_slot() == save_state_->game_slot());
        ability_hooks_->set_enforce(armed && slot_ok);
    }
    seed_kear_blocks_from_ap(); // received kear-block items -> lock removals (no-op when none received)
    upgrades_.recompute(state_);
    if (upgrades_.dirty() && tracker_ && pal::apply_upgrades(upgrades_.counts(), tracker_->player()))
        upgrades_.mark_applied(); // applied to the save; retry next tick if player not ready yet
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

void App::seed_kear_blocks_from_ap()
{
    if (!lock_hooks_)
        return;
    // A kear-block item's engine id IS the KeyBlock slot; set_removed is idempotent, and the existing
    // seed pass + KeyBlock::Update hook open the lock and persist the bit.
    for (const auto &it : state_.received_items())
        if (is_kear_block_item(it.item_id))
        {
            lock_hooks_->locks().set_removed(kear_block_engine_id(it.item_id));
            // special case for the only double lock in the game.
            if (it.item_id == kMMFirstDoubleKearBlockID)
                lock_hooks_->locks().set_removed(kear_block_engine_id(kMMSecondDoubleKearBlockID));
        }
}

void App::drain_grants()
{
    if (lock_hooks_)
        lock_hooks_->seed_removed_locks();
    if (ability_hooks_)
        ability_hooks_->enforce_train_tick();
    if (granter_)
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

void App::connect(const std::string &server, const std::string &slot, const std::string &password)
{
    link_->connect(server, slot, password);
}

void App::disconnect()
{
    link_->disconnect();
}

ConnectionStatus App::connection_status() const
{
    return ConnectionStatus{state_.phase(), state_.detail()};
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
    if (goal_tracker_)
        for (const auto &l : goal_tracker_->status_lines())
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
    if (granter_->grant(item_type))
        pal::logf(pal::LogLevel::Info, "console: giveapitem %lld (type=%d) granted", static_cast<long long>(ap_item_id), item_type);
    else
        pal::logf(pal::LogLevel::Warn, "console: giveapitem %lld not ready (collect any pickup first to capture player + position)",
                  static_cast<long long>(ap_item_id));
}

void App::remove_lock(int slot)
{
    lock_hooks_->locks().set_removed(slot);
    pal::logf(pal::LogLevel::Info, "console: removelock %d (live if spawned; opens on entry otherwise)", slot);
}

void App::set_modifier(int idx, bool on)
{
    policy_.arm_console_modifiers();
    if (modifier_hooks_)
        modifier_hooks_->set_live(idx, on);
    pal::logf(pal::LogLevel::Info, "console: modifier %d %s", idx, on ? "on" : "off");
}

void App::lock_modifiers(bool armed)
{
    policy_.arm_console_modifiers();
    if (modifier_hooks_)
        modifier_hooks_->set_armed(armed);
    pal::logf(pal::LogLevel::Info, "console: modifiers %s", armed ? "locked" : "unlocked");
}

void App::set_stat_caps(int attack, int defense, int sidearm)
{
    policy_.arm_forced_caps();
    if (level_cap_hooks_)
        level_cap_hooks_->set_counts(attack, defense, sidearm);
    pal::logf(pal::LogLevel::Info, "console: stat caps attack=%d defense=%d sidearm=%d", attack, defense, sidearm);
}

void App::set_ability_randomized(Ability a, bool on)
{
    policy_.arm_console_abilities();
    if (ability_hooks_)
        ability_hooks_->set_randomized(a, on);
    pal::logf(pal::LogLevel::Info, "console: ability %d randomized %s", static_cast<int>(a), on ? "on" : "off");
}

void App::enable_deathlink(bool on)
{
    if (link_)
        link_->enable_deathlink(on);
    pal::logf(pal::LogLevel::Info, "console: deathlink %s", on ? "enabled" : "disabled");
}
} // namespace mth
