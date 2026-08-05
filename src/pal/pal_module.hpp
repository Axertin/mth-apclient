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

// Executable-code range of the game module. A module-wide range is NOT sufficient: the mod API
// struct and the statics next to it live inside the module, so only an executable-section test
// distinguishes a real function pointer from adjacent static data read past the struct's end.
struct TextRange
{
    std::uintptr_t base{0};
    std::size_t size{0};
};

// Platform-defined; call once at init and publish via set_game_text_range().
TextRange game_text_range();

inline TextRange &text_range_storage() noexcept
{
    static TextRange r{};
    return r;
}

inline void set_game_text_range(TextRange r) noexcept
{
    text_range_storage() = r;
}

// True when no range has been published: tests have no game module, and a failed range lookup must
// not disable every API call.
[[nodiscard]] inline bool in_game_text(const void *p) noexcept
{
    const TextRange r = text_range_storage();
    if (r.size == 0)
        return p != nullptr;
    const auto v = reinterpret_cast<std::uintptr_t>(p);
    return v >= r.base && v < r.base + r.size;
}

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
