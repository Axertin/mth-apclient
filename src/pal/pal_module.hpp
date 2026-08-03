#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
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

// Symbols this platform hooks that the other platform does not: the two compilers inline
// different functions, so each PAL has its own small set on top of the shared list. Consumed by
// the AP gate's startup validation.
[[nodiscard]] std::span<const char *const> required_platform_symbols();

// Reads the full symbol table (incl. local symbols); requires init_hook_engine() first.
// Returns 0 if not found.
std::uintptr_t resolve_game_symbol(const char *mangled_name);

// Pass nullptr to clear; empty resolver falls back to platform default.
using SymbolResolver = std::function<std::uintptr_t(const char *)>;
void set_symbol_resolver(SymbolResolver);

} // namespace pal
