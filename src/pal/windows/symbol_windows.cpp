#include <span>

#include "mth/core/data/game_symbols.hpp"
#include "pal/pal_module.hpp"
#include "pal/windows/signature_scan.hpp"

namespace pal
{

namespace
{
SymbolResolver g_custom;
}

void set_symbol_resolver(SymbolResolver r)
{
    g_custom = std::move(r);
}

std::uintptr_t resolve_game_symbol(const char *mangled_name)
{
    if (g_custom)
        return g_custom(mangled_name);
    return scan_resolve(mangled_name);
}

namespace
{
// See pal_module.hpp. Kept next to the resolver so a platform's list cannot drift
// away from the platform that consumes it.
constexpr const char *kPlatformSymbols[] = {
    mth::sym::level_up_menu_update,
    mth::sym::shop_init_state,
};
} // namespace

std::span<const char *const> required_platform_symbols()
{
    return kPlatformSymbols;
}

} // namespace pal
