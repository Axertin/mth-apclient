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

} // namespace pal
