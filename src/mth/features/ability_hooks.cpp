#include "mth/features/ability_hooks.hpp"

#include <utility>

#include "mod/mod_api.hpp"
#include "mth/core/data/game_symbols.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

namespace mth
{

AbilityHooks::AbilityHooks(std::function<bool(std::int64_t item_id)> is_granted) : is_granted_(std::move(is_granted))
{
    g_save_manager_ = pal::resolve_game_symbol(sym::save_manager);
    if (g_save_manager_ == 0)
        pal::logf(pal::LogLevel::Warn, "AbilityHooks: g_saveManager not resolved; train enforcement disabled");

    const bool installed = pal::install_ability_hooks([this](int ordinal)
                                                      { return gate_.blocked(static_cast<Ability>(ordinal), AbilityGate::GrantQuery{enforce_, is_granted_}); });
    if (!installed)
        pal::logf(pal::LogLevel::Warn, "AbilityHooks: no ability chokepoints resolved; gating disabled");

    // Track the burrow lifetime from the two commit sites the mod already hooks, so the boundary poll knows
    // which mode the player is in without reading the mode field (its offset drifts between builds) (#163).
    pal::set_burrow_observers([this](bool deep) { burrow_.arm(deep); }, [this] { burrow_.disarm(); });
}

AbilityHooks::~AbilityHooks()
{
    pal::remove_ability_hooks();
}

void AbilityHooks::set_randomized(Ability a, bool on)
{
    gate_.set_randomized(a, on);
}

void AbilityHooks::set_enforce(bool on)
{
    enforce_ = on;
}

void AbilityHooks::set_train_gate(bool rando_active, std::uint32_t line_mask)
{
    train_rando_active_ = rando_active;
    train_mask_ = line_mask;
    pal::set_train_destination_gate(line_mask, rando_active); // published to the OnNPCEvent detour
}

void AbilityHooks::enforce_train_tick()
{
    // train_rando: boarding requires the generic Train Pass (#98), and each destination is gated on its AP
    // ticket - the +0x1e0 clamp undoes the footfall auto-unlock (box UX), while the OnNPCEvent detour (fed
    // by set_train_gate) refuses the warp for un-granted lines. Otherwise fall back to the whole-train
    // ability gate (console-driven Train ability), which hides the conductor while blocked.
    if (train_rando_active_)
    {
        pal::enforce_train_boarding(g_save_manager_);
        pal::enforce_train_destinations(g_save_manager_, train_mask_);
    }
    else
    {
        pal::enforce_train_presence(g_save_manager_, gate_.blocked(Ability::Train, AbilityGate::GrantQuery{enforce_, is_granted_}));
    }
}

void AbilityHooks::enforce_burrow_tick(void *player)
{
    // Any change of player, or none at all, drops the arm: acting on a player who is not burrowing is worse
    // than the sequence break this closes.
    if (player == nullptr || player != last_player_)
    {
        last_player_ = player;
        burrow_.disarm();
        return;
    }
    if (mod::pause_api_available() && mod::world_is_paused())
        return; // frozen frame: hold the confirmation rather than advancing it on a stale reading
    const bool burrowing = pal::player_is_burrowing(player);
    burrow_.observe_burrowing(burrowing);
    if (!burrowing || !enforce_ || !burrow_.armed())
        return;
    const int water = pal::burrow_water_state(player);
    if (water < 0)
    {
        burrow_.reading_unavailable();
        return;
    }

    const AbilityGate::GrantQuery q{enforce_, is_granted_};
    switch (burrow_.observe(water == 1, gate_.blocked(Ability::Burrow, q), gate_.blocked(Ability::Swim, q)))
    {
    case BoundaryAction::FallIn:
        pal::logf(pal::LogLevel::Info, "abilities: burrowed into deep water without Swim -> falling in (%d)", pal::request_deep_water_fall(player) ? 1 : 0);
        break;
    case BoundaryAction::Emerge:
        pal::logf(pal::LogLevel::Info, "abilities: swam onto land without Burrow -> surfacing (%d)", pal::force_burrow_emerge(player) ? 1 : 0);
        break;
    case BoundaryAction::None:
        break;
    }
}

void AbilityHooks::on_world_destroy()
{
    burrow_.disarm();
    last_player_ = nullptr;
}

} // namespace mth
