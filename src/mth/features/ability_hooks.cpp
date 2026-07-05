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

void AbilityHooks::enforce_train_tick()
{
    pal::enforce_train_presence(g_save_manager_, gate_.blocked(Ability::Train, AbilityGate::GrantQuery{enforce_, is_granted_}));
}

} // namespace mth
