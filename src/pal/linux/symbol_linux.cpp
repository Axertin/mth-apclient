#include <frida-gum.h>

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

    // Default: read the main module's full symbol table (incl. local symbols).
    const GumModuleDetails *m = gum_process_get_main_module();
    if (m == nullptr || mangled_name == nullptr)
        return 0;
    const GumAddress addr = gum_module_find_symbol_by_name(m->name, mangled_name);
    return static_cast<std::uintptr_t>(addr);
}

} // namespace pal
