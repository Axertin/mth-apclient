#include "pal/pal_module.hpp"

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
    // The shipped PE has no symbol table; the future frida-on-Windows / byte-pattern
    // scanner fills this in. Until then, resolution fails here (hooks log + skip).
    return 0;
}

} // namespace pal
