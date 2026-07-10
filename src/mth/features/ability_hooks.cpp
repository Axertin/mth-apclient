#include "mth/features/ability_hooks.hpp"

#include <utility>

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
    // ticket -- the +0x1e0 clamp undoes the footfall auto-unlock (box UX), while the OnNPCEvent detour (fed
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

} // namespace mth
