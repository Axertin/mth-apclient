#include "mth/app/hook_manager.hpp"

#include <cstdint>

#include "mth/core/ap/ap_ids.hpp"
#include "mth/core/ap/ap_state.hpp"
#include "mth/core/game_events.hpp"
#include "mth/core/modifier_config.hpp"
#include "mth/core/rando_bridge.hpp"
#include "mth/core/scout_registry.hpp"
#include "mth/core/session_policy.hpp"
#include "mth/features/ability_hooks.hpp"
#include "mth/features/boss_tracker.hpp"
#include "mth/features/chest_hooks.hpp"
#include "mth/features/death_hooks.hpp"
#include "mth/features/fountain_lamp_hooks.hpp"
#include "mth/features/goal_tracker.hpp"
#include "mth/features/item_granter.hpp"
#include "mth/features/levelcap_hooks.hpp"
#include "mth/features/location_hooks.hpp"
#include "mth/features/lock_hooks.hpp"
#include "mth/features/modifier_hooks.hpp"
#include "mth/features/pawn_shop_hooks.hpp"
#include "mth/features/save_takeover.hpp"
#include "mth/features/title_gate.hpp"
#include "mth/hooks/game_hooks.hpp"
#include "pal/pal_game.hpp"

namespace mth
{

HookManager::HookManager(IGameEvents &events, RandoBridge &rando, ScoutRegistry &scout, ApState &state, std::function<void()> send_death,
                         std::function<void *()> get_player)
    : rando_(rando)
{
    game_hooks_ = std::make_unique<GameHooks>(events);
    location_hooks_ = std::make_unique<LocationHooks>(rando, &scout);
    boss_tracker_ = std::make_unique<BossTracker>(rando);
    goal_tracker_ = std::make_unique<GoalTracker>(rando);
    lock_hooks_ = std::make_unique<LockHooks>();
    chest_hooks_ = std::make_unique<ChestHooks>(lock_hooks_->locks()); // shares the lock registry + seed
    get_player_ = get_player;                                          // shared with the vanilla-kear credit (credit_kear_key)
    location_hooks_->set_player_getter(get_player);                    // kear key edits mirror Player+0x11b0 (#130)
    death_hooks_ = std::make_unique<DeathHooks>(std::move(send_death), std::move(get_player));
    ability_hooks_ = std::make_unique<AbilityHooks>([&state](std::int64_t id) { return state.has_received(id); });
    auto connected = [&state] { return state.phase() == ConnectionPhase::Connected; };
    pawn_shop_hooks_ = std::make_unique<PawnShopHooks>(connected);
    modifier_hooks_ = std::make_unique<ModifierHooks>(ModifierRequest{});
    level_cap_hooks_ = std::make_unique<LevelCapHooks>();
    fountain_lamp_hooks_ = std::make_unique<FountainLampHooks>();
    // Before TitleGate, which takes the claim callback: TitleGate owns the only StartGame detour, and
    // reverse-order destruction then tears that detour down before the takeover it calls into.
    save_takeover_ = std::make_unique<SaveTakeover>(ApSaveStore(pal::mod_save_dir()),
                                                    [&state] { return std::make_pair(state.seed(), std::to_string(state.player_slot())); });
    title_gate_ = std::make_unique<TitleGate>(connected, [this] { return save_takeover_->begin(); });
}

HookManager::~HookManager()
{
    // GameHooks (the FixedUpdate/World::Update tick source) is torn down FIRST so no tick
    // fires into partially-destroyed feature hooks; then the feature hooks in App's original
    // relative order (chest before lock - chest references lock's registry).
    game_hooks_.reset();
    ability_hooks_.reset();
    pawn_shop_hooks_.reset();
    death_hooks_.reset();
    modifier_hooks_.reset();
    level_cap_hooks_.reset();
    title_gate_.reset(); // owns the StartGame detour that calls into the takeover, so it goes first
    save_takeover_.reset();
    fountain_lamp_hooks_.reset();
    goal_tracker_.reset();
    boss_tracker_.reset();
    location_hooks_.reset();
    chest_hooks_.reset();
    lock_hooks_.reset();
}

void HookManager::tick(ApState &state, SessionPolicy &policy, int save_game_slot)
{
    const bool authed = state.authenticated();
    // Enforce (seed + lockdown) only in an AP session, offline test mode, or once the console
    // drove modifiers; ap_scoped (authed only) restricts the seed to the captured AP-game slot.
    modifier_hooks_->set_enforce_live(policy.enforce_modifiers(authed));
    modifier_hooks_->set_ap_scoped(authed);
    modifier_hooks_->set_ossex_start(state.ossex_start()); // force Landing Done when slot_data requests it
    modifier_hooks_->drain_live();

    level_cap_hooks_->set_enforce_live(policy.enforce_caps(authed));
    if (!policy.caps_fixed())
        level_cap_hooks_->recompute(state);

    // Neutralize the world-kear collect grant in every AP mode; pin usable keys to 0 only in the AP-item
    // modes (vanilla mode credits received Universal Kears instead - see App/InboundGranter).
    location_hooks_->set_kear_gating(authed, authed && state.kear_keys_suppressed());
    location_hooks_->reconcile_kear_keys(); // re-cancel AP kears that a reload restored as usable keys (suppress modes)
    location_hooks_->enforce_native_bits(); // native collected-bit for server-collected durable-bit chests (Collect/coop)

    // slot_data lamps (0 when not authed) OR'd with the sticky console override (works offline).
    fountain_lamp_hooks_->set_lit_mask((authed ? state.lit_generator_lamp_mask() : 0) | lamp_console_override_.load(std::memory_order_relaxed));

    // Unconditional: the bridge persists and queues checks while disconnected, so a kill during an
    // outage is still recorded rather than lost.
    boss_tracker_->poll();

    if (authed)
        goal_tracker_->evaluate(state); // poll SaveSlot; fires the AP goal when the slot_data condition is met

    if (authed)
    {
        ability_hooks_->set_randomized(Ability::Burrow, state.burrow_rando());
        ability_hooks_->set_randomized(Ability::Swim, state.swim_rando());
        ability_hooks_->set_randomized(Ability::RopeClimb, state.rope_rando());
        ability_hooks_->set_randomized(Ability::BouncePuff, state.puff_rando());
        ability_hooks_->set_randomized(Ability::BounceSpring, state.spring_rando());
        ability_hooks_->set_randomized(Ability::Carry, state.carry_rando());
    }
    // offline: leave randomized as the `ability` console command set it.

    // Train fast-travel (train_rando): gate each destination on its AP ticket rather than the whole-train
    // ability, so visiting a station no longer unlocks it (#98). Build the granted-line mask from received
    // ticket itemTypes; enforce_train_tick clamps the SaveSlot bitfield to it each frame.
    std::uint32_t train_mask = 0;
    for (const auto &it : state.received_items())
        if (is_vanilla_game_item(it.item_id))
            train_mask |= train_ticket_bit(game_item_type(it.item_id));
    // The station's donation machine (#162). When the seed carries it as a location the vanilla path is
    // right - it completes, OnPickupDone suppresses the pass and sends the check - so all it needs is a
    // cheaper goal. When the seed does not, nothing would refuse the grant, so the machine is made inert
    // and the OnPickup backstop arms behind it.
    const bool train_gated = authed && state.train_rando();
    const bool machine_is_check = state.is_valid_location(ap_loc_id(kTrainPassLocIdx));
    ability_hooks_->set_train_gate(train_gated, train_mask);
    ability_hooks_->set_ticket_machine(machine_is_check, rando_.is_checked(kTrainPassLocIdx), ticket_machine_seed(state.train_pass_cost()));
    set_train_pass_machine_blocked(train_gated && !machine_is_check);
    const bool armed = policy.enforce_abilities(authed);
    const bool slot_ok = !authed ? true : (save_game_slot >= 0 && modifier_hooks_->captured_ap_slot() == save_game_slot);
    ability_hooks_->set_enforce(armed && slot_ok);

    seed_kear_blocks(state);

    death_hooks_->poll(); // edge-detect a local death for deathlink (send_death gates on deathlink enabled)

    save_takeover_->tick(); // self-gates on its own step; no-ops in the terminal ones
}

void HookManager::clear_session_state()
{
    // seed_kear_blocks() only ever adds, so the previous connection's kear-block removals would otherwise
    // keep being written into the next AP game's unlock bitfield.
    lock_hooks_->locks().clear();
    modifier_hooks_->set_ap_slot(-1); // unknown again: the next AP game captures its own slot
}

bool HookManager::credit_kear_key()
{
    return location_hooks_->credit_kear_key(get_player_ ? get_player_() : nullptr);
}

void HookManager::seed_kear_blocks(ApState &state)
{
    for (const auto &it : state.received_items())
        if (is_kear_block_item(it.item_id))
        {
            lock_hooks_->locks().set_removed(kear_block_engine_id(it.item_id));
            if (it.item_id == kMMFirstDoubleKearBlockID)
                lock_hooks_->locks().set_removed(kear_block_engine_id(kMMSecondDoubleKearBlockID));
        }
}

void HookManager::drain()
{
    lock_hooks_->seed_removed_locks();
    lock_hooks_->sweep_locks(); // open already-spawned KeyBlock / KeyBlockChain locks
    chest_hooks_->sweep();      // clear the kear-lock on already-spawned chests
    ability_hooks_->enforce_train_tick();
    ability_hooks_->enforce_burrow_tick(get_player_ ? get_player_() : nullptr);
}

void HookManager::on_world_destroy()
{
    location_hooks_->reset_native_bits(); // a save reload clears s_rItemCollection; re-apply on the next load
    ability_hooks_->on_world_destroy();
    chest_hooks_->on_world_destroy(); // the tracked chests died with the world
}

void HookManager::kill_player()
{
    death_hooks_->kill();
}

int HookManager::captured_ap_slot() const
{
    return modifier_hooks_->captured_ap_slot();
}

void HookManager::set_ap_slot(int slot)
{
    modifier_hooks_->set_ap_slot(slot);
}

void HookManager::set_modifier_live(int idx, bool on)
{
    modifier_hooks_->set_live(idx, on);
}

void HookManager::set_modifiers_armed(bool armed)
{
    modifier_hooks_->set_armed(armed);
}

void HookManager::remove_lock(int slot)
{
    lock_hooks_->locks().set_removed(slot);
}

void HookManager::set_stat_caps(int attack, int defense, int sidearm)
{
    level_cap_hooks_->set_counts(attack, defense, sidearm);
}

void HookManager::set_ability_randomized(Ability a, bool on)
{
    ability_hooks_->set_randomized(a, on);
}

void HookManager::set_lamp_console_override(std::uint32_t mask)
{
    lamp_console_override_.store(mask, std::memory_order_relaxed); // render thread; applied next game-thread tick
}

bool HookManager::takeover_active() const
{
    return save_takeover_->takeover_active();
}

void HookManager::append_status_lines(std::vector<std::string> &out) const
{
    for (const auto &l : modifier_hooks_->status_lines())
        out.push_back(l);
    for (const auto &l : level_cap_hooks_->status_lines())
        out.push_back(l);
    for (const auto &l : goal_tracker_->status_lines())
        out.push_back(l);
    for (const auto &l : save_takeover_->status_lines())
        out.push_back(l);
}

} // namespace mth
