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
#include "mth/features/boss_hooks.hpp"
#include "mth/features/chest_hooks.hpp"
#include "mth/features/death_hooks.hpp"
#include "mth/features/fountain_lamp_hooks.hpp"
#include "mth/features/goal_tracker.hpp"
#include "mth/features/levelcap_hooks.hpp"
#include "mth/features/location_hooks.hpp"
#include "mth/features/lock_hooks.hpp"
#include "mth/features/modifier_hooks.hpp"
#include "mth/features/pawn_shop_hooks.hpp"
#include "mth/hooks/game_hooks.hpp"

namespace mth
{

HookManager::HookManager(IGameEvents &events, RandoBridge &rando, ScoutRegistry &scout, ApState &state, std::function<void()> send_death,
                         std::function<void *()> get_player)
{
    game_hooks_ = std::make_unique<GameHooks>(events);
    location_hooks_ = std::make_unique<LocationHooks>(rando, &scout);
    boss_hooks_ = std::make_unique<BossHooks>(rando);
    goal_tracker_ = std::make_unique<GoalTracker>(rando);
    lock_hooks_ = std::make_unique<LockHooks>();
    chest_hooks_ = std::make_unique<ChestHooks>(lock_hooks_->locks()); // shares the lock registry + seed
    death_hooks_ = std::make_unique<DeathHooks>(std::move(send_death), std::move(get_player));
    ability_hooks_ = std::make_unique<AbilityHooks>([&state](std::int64_t id) { return state.has_received(id); });
    pawn_shop_hooks_ = std::make_unique<PawnShopHooks>([&state] { return state.phase() == ConnectionPhase::Connected; });
    modifier_hooks_ = std::make_unique<ModifierHooks>(ModifierRequest{});
    level_cap_hooks_ = std::make_unique<LevelCapHooks>();
    fountain_lamp_hooks_ = std::make_unique<FountainLampHooks>();
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
    fountain_lamp_hooks_.reset();
    goal_tracker_.reset();
    location_hooks_.reset();
    boss_hooks_.reset();
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

    location_hooks_->set_kear_rando(state.kear_rando()); // slot_data flag: neutralize the world-kear key grant
    location_hooks_->reconcile_kear_keys();              // re-cancel AP kears that a reload restored as usable keys
    location_hooks_->enforce_native_bits();              // native collected-bit for server-collected durable-bit chests (Collect/coop)

    // slot_data lamps (0 when not authed) OR'd with the sticky console override (works offline).
    fountain_lamp_hooks_->set_lit_mask((authed ? state.lit_generator_lamp_mask() : 0) | lamp_console_override_.load(std::memory_order_relaxed));

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
    ability_hooks_->set_train_gate(authed && state.train_rando(), train_mask);
    const bool armed = policy.enforce_abilities(authed);
    const bool slot_ok = !authed ? true : (save_game_slot >= 0 && modifier_hooks_->captured_ap_slot() == save_game_slot);
    ability_hooks_->set_enforce(armed && slot_ok);

    seed_kear_blocks(state);

    death_hooks_->poll(); // edge-detect a local death for deathlink (send_death gates on deathlink enabled)
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
    ability_hooks_->enforce_train_tick();
}

void HookManager::on_world_destroy()
{
    location_hooks_->reset_native_bits(); // a save reload clears s_rItemCollection; re-apply on the next load
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

void HookManager::append_status_lines(std::vector<std::string> &out) const
{
    for (const auto &l : modifier_hooks_->status_lines())
        out.push_back(l);
    for (const auto &l : level_cap_hooks_->status_lines())
        out.push_back(l);
    for (const auto &l : goal_tracker_->status_lines())
        out.push_back(l);
}

} // namespace mth
