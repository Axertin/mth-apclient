#include <span>

#include <frida-gum.h>

#include "mth/core/data/game_symbols.hpp"
#include "pal/pal_module.hpp"

namespace pal
{

namespace
{
SymbolResolver g_custom; // when set, overrides the gum default (tests / alt platforms)
}

void set_symbol_resolver(SymbolResolver r)
{
    g_custom = std::move(r);
}

std::uintptr_t resolve_game_symbol(const char *mangled_name)
{
    if (g_custom)
        return g_custom(mangled_name);

    const GumModuleDetails *m = gum_process_get_main_module();
    if (m == nullptr || mangled_name == nullptr)
        return 0;
    const GumAddress addr = gum_module_find_symbol_by_name(m->name, mangled_name);
    return static_cast<std::uintptr_t>(addr);
}

namespace
{
// See pal_module.hpp. Kept next to the resolver so a platform's list cannot drift
// away from the platform that consumes it.
constexpr const char *kPlatformSymbols[] = {
    mth::sym::chest_update,      mth::sym::get_new_game_max_level_player, mth::sym::key_block_chain_update, mth::sym::level_up_menu_update_state,
    mth::sym::shop_item_present,
};
} // namespace

std::span<const char *const> required_platform_symbols()
{
    return kPlatformSymbols;
}

} // namespace pal
