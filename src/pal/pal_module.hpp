#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace pal
{

struct ModuleInfo
{
    std::uintptr_t base{0};
    std::size_t size{0};
    std::string path{};
};

ModuleInfo game_module();
ModuleInfo self_module();

// game_module().base + relative (convenience).
std::uintptr_t rva(std::uintptr_t relative);

// Linux: GNU BuildID; Windows: PE TimeDateStamp:SizeOfImage hex string.
std::string game_build_id();

// nullptr module_basename consults the global symbol table only.
void *find_symbol(const char *module_basename, const char *symbol);

// Reads the full symbol table (incl. local symbols); requires init_hook_engine() first.
// Returns 0 if not found.
std::uintptr_t resolve_game_symbol(const char *mangled_name);

// Pass nullptr to clear; empty resolver falls back to platform default.
using SymbolResolver = std::function<std::uintptr_t(const char *)>;
void set_symbol_resolver(SymbolResolver);

} // namespace pal
