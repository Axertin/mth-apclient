#include "pal/windows/signature_scan.hpp"

#include <cstring>
#include <span>
#include <string>
#include <unordered_map>

#include "mod/mod_api.hpp"
#include "mth/core/data/native_sym_names.hpp"
#include "mth/core/sig_scan.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

namespace pal
{

// Only failures are logged here. The AP gate resolves every required symbol at startup and logs
// each result itself, on both platforms, so a success line here would duplicate it 1:1. The
// failure classification below is NOT duplicated: the gate only reports that a symbol is missing,
// not whether it was a miss, an ambiguous pattern, or an out-of-range DataRef.
std::uintptr_t scan_resolve(const char *mangled_name)
{
    if (!mangled_name)
        return 0;

    // Not thread-safe; symbol resolution runs single-threaded during init,
    // matching the other PAL globals (g_custom, hook-engine globals).
    static std::unordered_map<std::string, std::uintptr_t> cache;
    if (auto it = cache.find(mangled_name); it != cache.end())
        return it->second;

    // Ask the game first for the names it exposes. That address is authoritative and cannot drift,
    // so it beats a carved signature; a build without the entry returns null and falls through to
    // the scan below, which keeps working on its own.
    if (const char *plain = mth::sym::native_sym_name(mangled_name); plain != nullptr)
    {
        if (void *addr = mod::sym_addr(plain); addr != nullptr)
        {
            cache[mangled_name] = reinterpret_cast<std::uintptr_t>(addr);
            return reinterpret_cast<std::uintptr_t>(addr);
        }
    }

    // g_saveManager is too hot to carve as a DataRef (~2300 xrefs). But the game's only
    // `cmove r9,[rip+g_saveManager]` (4c 0f 44 0d, 8-byte) - the "default a null SaveSlot* to the
    // active slot" idiom - is UNIQUE in .text, so scan the whole section for it and read its RIP
    // target. Survives function moves across builds (no anchor symbol needed); a future build that
    // adds a second such cmov would shift this to the first match (caught by the logged address).
    if (std::strcmp(mangled_name, "g_saveManager") == 0)
    {
        std::uintptr_t addr = 0;
        const TextRange tr = game_text_range();
        const std::span<const std::uint8_t> text{reinterpret_cast<const std::uint8_t *>(tr.base), tr.size};
        if (!text.empty())
        {
            static const std::uint8_t kCmove[] = {0x4c, 0x0f, 0x44, 0x0d};
            const std::uintptr_t text_base = reinterpret_cast<std::uintptr_t>(text.data());
            addr = mth::sig::find_riprel_load(text, text_base, kCmove, sizeof(kCmove), /*disp_off=*/4, /*insn_len=*/8);
        }
        if (addr == 0)
            logf(LogLevel::Error, "sig: g_saveManager not resolved (no `cmov r9,[rip]` in .text)");
        cache[mangled_name] = addr;
        return addr;
    }

    const TextRange tr = game_text_range();
    const std::span<const std::uint8_t> text{reinterpret_cast<const std::uint8_t *>(tr.base), tr.size};
    if (text.empty())
    {
        logf(LogLevel::Error, "sig: could not locate .text in game module");
        return 0;
    }
    const std::uintptr_t region_base = reinterpret_cast<std::uintptr_t>(text.data());

    for (const mth::sig::Entry &e : sig_table())
    {
        if (std::strcmp(e.name, mangled_name) != 0)
            continue;
        const std::uintptr_t addr = mth::sig::resolve(text, region_base, e);
        if (addr == 0)
        {
            // Classify the failure so a future game update is diagnosable: a miss
            // means the function changed/moved (the signature is now stale and,
            // crucially, fails loud instead of mis-hooking); ambiguous means the
            // pattern is no longer unique and must be re-carved longer.
            const mth::sig::Match m = mth::sig::find_masked(text, e.pattern, e.mask, e.len);
            const char *why = !m.found    ? "no .text match (function moved/changed?)"
                              : !m.unique ? "ambiguous: multiple .text matches"
                                          : "DataRef disp32 out of range";
            logf(LogLevel::Error, "sig: %s did not resolve (%s)", mangled_name, why);
        }
        cache[mangled_name] = addr;
        return addr;
    }

    logf(LogLevel::Error, "sig: no signature table entry for %s", mangled_name);
    cache[mangled_name] = 0;
    return 0;
}

} // namespace pal
