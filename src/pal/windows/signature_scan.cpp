#include "pal/windows/signature_scan.hpp"

#include <cstring>
#include <span>
#include <string>
#include <unordered_map>

#include "mth/core/sig_scan.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace pal
{

namespace
{
// Locate the loaded module's .text section as a (base_ptr, size) span.
// Runs only against the genuinely-loaded game image (game_module().base comes
// from GetModuleHandleW(nullptr), an OS-validated PE), so the header walk
// assumes well-formed DOS/NT headers beyond the magic checks below.
std::span<const std::uint8_t> text_section(std::uintptr_t module_base)
{
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(module_base);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return {};
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(module_base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return {};

    const auto *sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        // First section whose name starts with ".text" wins (Name is 8 bytes,
        // null-padded; the 5-byte prefix compare also accepts ".text$..").
        if (std::memcmp(sec[i].Name, ".text", 5) == 0)
        {
            const auto *start = reinterpret_cast<const std::uint8_t *>(module_base + sec[i].VirtualAddress);
            const std::size_t size = sec[i].Misc.VirtualSize;
            return {start, size};
        }
    }
    return {};
}
} // namespace

std::uintptr_t scan_resolve(const char *mangled_name)
{
    if (!mangled_name)
        return 0;

    // Not thread-safe; symbol resolution runs single-threaded during init,
    // matching the other PAL globals (g_custom, hook-engine globals).
    static std::unordered_map<std::string, std::uintptr_t> cache;
    if (auto it = cache.find(mangled_name); it != cache.end())
        return it->second;

    // g_saveManager is too hot to carve as a DataRef (~2300 xrefs). But the game's only
    // `cmove r9,[rip+g_saveManager]` (4c 0f 44 0d, 8-byte) -- the "default a null SaveSlot* to the
    // active slot" idiom -- is UNIQUE in .text, so scan the whole section for it and read its RIP
    // target. Survives function moves across builds (no anchor symbol needed); a future build that
    // adds a second such cmov would shift this to the first match (caught by the logged address).
    if (std::strcmp(mangled_name, "g_saveManager") == 0)
    {
        std::uintptr_t addr = 0;
        const std::span<const std::uint8_t> text = text_section(game_module().base);
        if (!text.empty())
        {
            static const std::uint8_t kCmove[] = {0x4c, 0x0f, 0x44, 0x0d};
            const std::uintptr_t text_base = reinterpret_cast<std::uintptr_t>(text.data());
            addr = mth::sig::find_riprel_load(text, text_base, kCmove, sizeof(kCmove), /*disp_off=*/4, /*insn_len=*/8);
        }
        if (addr == 0)
            logf(LogLevel::Error, "sig: g_saveManager not resolved (no `cmov r9,[rip]` in .text)");
        else
            logf(LogLevel::Info, "sig: resolved g_saveManager -> %p", reinterpret_cast<void *>(addr));
        cache[mangled_name] = addr;
        return addr;
    }

    const ModuleInfo gm = game_module();
    const std::span<const std::uint8_t> text = text_section(gm.base);
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
        else
            logf(LogLevel::Info, "sig: resolved %s -> %p", mangled_name, reinterpret_cast<void *>(addr));
        cache[mangled_name] = addr;
        return addr;
    }

    logf(LogLevel::Error, "sig: no signature table entry for %s", mangled_name);
    cache[mangled_name] = 0;
    return 0;
}

} // namespace pal
