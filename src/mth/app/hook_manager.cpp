#include "mth/app/hook_manager.hpp"

#include <cstdint>

#include "mth/core/ap/ap_ids.hpp"
#include "mth/core/ap/ap_state.hpp"
#include "mth/core/data/game_symbols.hpp"
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
#include "mth/features/intro_chest_gate.hpp"
#include "mth/features/item_granter.hpp"
#include "mth/features/kear_completion_tracker.hpp"
#include "mth/features/levelcap_hooks.hpp"
#include "mth/features/location_hooks.hpp"
#include "mth/features/lock_hooks.hpp"
#include "mth/features/modifier_hooks.hpp"
#include "mth/features/pawn_shop_hooks.hpp"
#include "mth/features/save_takeover.hpp"
#include "mth/features/sewer_cat_gate.hpp"
#include "mth/features/title_gate.hpp"
#include "mth/hooks/game_hooks.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

namespace mth
{

HookManager::HookManager(IGameEvents &events, RandoBridge &rando, ScoutRegistry &scout, ApState &state, ApSaveBundleStore &bundle,
                         std::function<void(const std::string &)> send_death, std::function<void *()> get_player)
    : rando_(rando)
{
    game_hooks_ = std::make_unique<GameHooks>(events);
    location_hooks_ = std::make_unique<LocationHooks>(rando, &scout);
    boss_tracker_ = std::make_unique<BossTracker>(rando);
    // Loading a save is the vendor lockout's only reset point, which scopes the latch to a save rather
    // than to the process.
    pal::set_save_loaded(
        [this]
        {
            boss_tracker_->on_save_loaded();
            vendor_lockout_.store(false, std::memory_order_relaxed);
        });
    goal_tracker_ = std::make_unique<GoalTracker>(rando);
    kear_completion_ = std::make_unique<KearCompletionTracker>();
    lock_hooks_ = std::make_unique<LockHooks>();
    chest_hooks_ = std::make_unique<ChestHooks>(lock_hooks_->locks()); // shares the lock registry + seed
    get_player_ = get_player;                                          // shared with the vanilla-kear credit (credit_kear_key)
    location_hooks_->set_player_getter(get_player);                    // kear key edits mirror Player+0x11b0 (#130)
    death_hooks_ = std::make_unique<DeathHooks>(std::move(send_death), std::move(get_player));
    ability_hooks_ = std::make_unique<AbilityHooks>([&state](std::int64_t id) { return state.has_received(id); });
    auto connected = [&state] { return state.phase() == ConnectionPhase::Connected; };
    // Pawnty and Panino both sell outside AP logic, so they stay shut for the rest of the save once a
    // session has been seen; a mid-run disconnect must not hand the exploit back. SewerCatGate::tick
    // calls this every drain, which is what arms the latch for Pawnty too: his own detour does not run
    // until the player reaches him.
    auto vendor_locked = [this, &state]
    {
        if (state.phase() == ConnectionPhase::Connected)
            vendor_lockout_.store(true, std::memory_order_relaxed);
        return vendor_lockout_.load(std::memory_order_relaxed);
    };
    pawn_shop_hooks_ = std::make_unique<PawnShopHooks>(vendor_locked);
    sewer_cat_gate_ = std::make_unique<SewerCatGate>(vendor_locked);
    intro_chest_gate_ = std::make_unique<IntroChestGate>(); // armed from enforce_weapon_grants, which builds the mask
    modifier_hooks_ = std::make_unique<ModifierHooks>(ModifierRequest{});
    level_cap_hooks_ = std::make_unique<LevelCapHooks>();
    fountain_lamp_hooks_ = std::make_unique<FountainLampHooks>();
    // Before TitleGate, which takes the claim callback: TitleGate owns the only StartGame detour, and
    // reverse-order destruction then tears that detour down before the takeover it calls into.
    save_takeover_ = std::make_unique<SaveTakeover>(bundle, [&state] { return std::make_pair(state.seed(), std::to_string(state.player_slot())); });
    title_gate_ = std::make_unique<TitleGate>(connected, [this] { return save_takeover_->begin(); });
    save_manager_ = pal::resolve_game_symbol(sym::save_manager);
    if (save_manager_ == 0)
        pal::logf(pal::LogLevel::Warn, "starter: g_saveManager not resolved; the starter-swap clear and the weapon ownership clamp are disabled");
}

HookManager::~HookManager()
{
    // Unreached while the entry point leaks App (see entry.cpp). Explicit rather than reverse-declaration
    // order: GameHooks (the FixedUpdate/World::Update tick source) FIRST so no tick fires into a
    // half-destroyed feature hook, then App's own order (chest before lock - chest references lock's registry).
    game_hooks_.reset();
    ability_hooks_.reset();
    pawn_shop_hooks_.reset();
    sewer_cat_gate_.reset();
    intro_chest_gate_.reset();
    death_hooks_.reset();
    modifier_hooks_.reset();
    level_cap_hooks_.reset();
    title_gate_.reset(); // owns the StartGame detour that calls into the takeover, so it goes first
    save_takeover_.reset();
    fountain_lamp_hooks_.reset();
    goal_tracker_.reset();
    pal::set_save_loaded(nullptr); // the callback captures this; drop it before the tracker dies
    boss_tracker_.reset();
    kear_completion_.reset();
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
    {
        goal_tracker_->evaluate(state);    // poll SaveSlot; fires the AP goal when the slot_data condition is met
        kear_completion_->evaluate(state); // latches the KeyMiser trade flag once every kear is held (#174)
    }

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
    // Through the bridge, not ApState, so a seed that prunes the donation location is seen here too.
    const bool machine_is_check = rando_.is_ap_location(kTrainPassLocIdx);
    ability_hooks_->set_train_gate(train_gated, train_mask);
    ability_hooks_->set_ticket_machine(machine_is_check, rando_.is_checked(kTrainPassLocIdx), ticket_machine_seed(state.train_pass_cost()));
    set_train_pass_machine_blocked(train_gated && !machine_is_check);
    const bool armed = policy.enforce_abilities(authed);
    const bool slot_ok = !authed ? true : (save_game_slot >= 0 && modifier_hooks_->captured_ap_slot() == save_game_slot);
    ability_hooks_->set_enforce(armed && slot_ok);
    // Both calls write durable save fields, so both take authed as well: slot_ok on its own is true while
    // offline and is not the bound-save test.
    pal::clear_starter_weapon_swap(save_manager_, authed, slot_ok);
    enforce_weapon_grants(state, authed, slot_ok);

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
    death_hooks_->reset();            // a death latched for retry belongs to the connection being dropped
}

void HookManager::clear_pending_death()
{
    death_hooks_->reset();
}

bool HookManager::credit_kear_key()
{
    return location_hooks_->credit_kear_key(get_player_ ? get_player_() : nullptr);
}

void HookManager::seed_kear_blocks(ApState &state)
{
    for (const auto &it : state.received_items())
    {
        if (is_kear_block_item(it.item_id))
        {
            lock_hooks_->locks().set_removed(kear_block_engine_id(it.item_id));
            if (it.item_id == kMMFirstDoubleKearBlockID)
                lock_hooks_->locks().set_removed(kear_block_engine_id(kMMSecondDoubleKearBlockID));
        }
        if (is_area_lock_item(it.item_id))
        {
            for (const auto &g : kAreaLockGroups)
                if (g.item_id == it.item_id)
                    for (const auto &lock : g.locks)
                        lock_hooks_->locks().set_removed(kear_block_engine_id(lock));
        }
    }
}

// The seed precollects the starting weapon, so every weapon reaches the player through the item stream. The
// game hands one out anyway at the intro (the weapon chest's pick, and the fallback that force-grants the
// whip), and both write the SaveSlot fields directly rather than going through Items::OnPickupDone, where the
// vanilla-grant suppression sits. So ownership is clamped to the AP grants each tick instead of hooking
// either site.
void HookManager::enforce_weapon_grants(ApState &state, bool authed, bool slot_ok)
{
    WeaponTally tally;
    for (const auto &it : state.received_items())
        tally.add(it.item_id);

    std::uint32_t authorized[kWeaponFamilyCount]{};
    for (int fam = 0; fam < kWeaponFamilyCount; ++fam)
        authorized[fam] = tally.owned_mask(fam);
    pal::enforce_weapon_ownership(save_manager_, authorized, authed, slot_ok);
    // The clamp corrects the bits after the fact; the chest itself still offers all three starters and still
    // equips the pick, so it is demoted as well. Both extra conditions are load-bearing: the weapon-change
    // mode lists owned weapons only, so demoting before a grant lands, or on a save AP does not own, leaves
    // the player with nothing to arm.
    intro_chest_gate_->set_armed(authed && slot_ok && any_weapon_authorized(authorized));
}

void HookManager::drain()
{
    lock_hooks_->seed_removed_locks();
    lock_hooks_->sweep_locks(); // open already-spawned KeyBlock / KeyBlockChain locks
    chest_hooks_->sweep();      // clear the kear-lock on already-spawned chests
    ability_hooks_->enforce_train_tick();
    ability_hooks_->enforce_burrow_tick(get_player_ ? get_player_() : nullptr);
    sewer_cat_gate_->tick();   // self-gated on the vendor lockout; walks nothing when it is clear
    intro_chest_gate_->tick(); // self-gated on the armed flag; walks nothing when it is clear
}

void HookManager::on_world_update_end(void *world)
{
    save_takeover_->on_world_update_end(world);
}

void HookManager::on_world_destroy()
{
    location_hooks_->reset_native_bits(); // a save reload clears s_rItemCollection; re-apply on the next load
    ability_hooks_->on_world_destroy();
    chest_hooks_->on_world_destroy(); // the tracked chests died with the world
    sewer_cat_gate_->on_world_destroy();
    intro_chest_gate_->on_world_destroy();
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
