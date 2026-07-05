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

std::uintptr_t rva(std::uintptr_t relative)
{
    return game_module().base + relative;
}

void *find_symbol(const char *module_basename, const char *symbol)
{
    if (!symbol)
        return nullptr;
    if (module_basename)
    {
        if (void *h = dlopen(module_basename, RTLD_LAZY | RTLD_NOLOAD))
        {
            void *sym = dlsym(h, symbol);
            dlclose(h);
            if (sym)
                return sym;
        }
    }
    return dlsym(RTLD_DEFAULT, symbol);
}

} // namespace pal
