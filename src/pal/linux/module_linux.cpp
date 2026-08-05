#include <cstdio>
#include <cstring>
#include <string>

#include <dlfcn.h>
#include <elf.h>
#include <link.h>

#include "pal/pal_module.hpp"

namespace
{

struct MainModuleMatch
{
    pal::ModuleInfo info;
    pal::TextRange exec{};
    bool found{false};
};

int dlpi_collect_main(struct dl_phdr_info *info, size_t /*size*/, void *data)
{
    auto *out = static_cast<MainModuleMatch *>(data);
    if (out->found)
        return 1;

    // Non-PIE: dlpi_addr=0, p_vaddrs absolute. PIE/SO: dlpi_addr=load offset, p_vaddrs relative.
    std::uintptr_t lo = static_cast<std::uintptr_t>(-1);
    std::uintptr_t hi = 0;
    for (int i = 0; i < info->dlpi_phnum; ++i)
    {
        const auto &ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_LOAD)
            continue;
        if (ph.p_vaddr < lo)
            lo = ph.p_vaddr;
        const auto end = ph.p_vaddr + ph.p_memsz;
        if (end > hi)
            hi = end;
        // Largest executable PT_LOAD is the code segment; avoids small exec-but-not-code segments.
        if ((ph.p_flags & PF_X) != 0 && ph.p_memsz > out->exec.size)
        {
            out->exec.base = static_cast<std::uintptr_t>(info->dlpi_addr) + ph.p_vaddr;
            out->exec.size = ph.p_memsz;
        }
    }
    if (lo == static_cast<std::uintptr_t>(-1))
        lo = 0;

    out->info.base = static_cast<std::uintptr_t>(info->dlpi_addr) + lo;
    out->info.size = hi - lo;
    out->info.path = info->dlpi_name ? info->dlpi_name : "";
    out->found = true;
    // dlpi_name "" is the main executable (glibc convention); keep iterating if first entry has a name.
    return out->info.path.empty() ? 1 : 0;
}

} // namespace

namespace pal
{

ModuleInfo game_module()
{
    MainModuleMatch m{};
    dl_iterate_phdr(&dlpi_collect_main, &m);
    return m.info;
}

TextRange game_text_range()
{
    MainModuleMatch m{};
    dl_iterate_phdr(&dlpi_collect_main, &m);
    return m.exec;
}

ModuleInfo self_module()
{
    ModuleInfo info{};
    Dl_info di{};
    if (dladdr(reinterpret_cast<const void *>(&self_module), &di) != 0)
    {
        info.base = reinterpret_cast<std::uintptr_t>(di.dli_fbase);
        info.path = di.dli_fname ? di.dli_fname : "";
    }
    return info;
}

} // namespace pal
