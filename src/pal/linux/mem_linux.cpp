#include <cstdint>
#include <cstring>

#include <sys/mman.h>
#include <unistd.h>

#include "pal/pal_mem.hpp"

namespace pal
{

namespace
{
struct PageSpan
{
    void *start;
    std::size_t len;
};
PageSpan page_span(void *addr, std::size_t len)
{
    const long ps = sysconf(_SC_PAGESIZE);
    const auto a = reinterpret_cast<std::uintptr_t>(addr);
    const std::uintptr_t start = a & ~static_cast<std::uintptr_t>(ps - 1);
    const std::uintptr_t end = (a + len + ps - 1) & ~static_cast<std::uintptr_t>(ps - 1);
    return {reinterpret_cast<void *>(start), end - start};
}
} // namespace

bool make_writable(void *addr, std::size_t len)
{
    const PageSpan p = page_span(addr, len);
    return mprotect(p.start, p.len, PROT_READ | PROT_WRITE) == 0;
}

bool patch_code(void *addr, const void *bytes, std::size_t len)
{
    const PageSpan p = page_span(addr, len);
    if (mprotect(p.start, p.len, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
        return false;
    std::memcpy(addr, bytes, len);
    __builtin___clear_cache(reinterpret_cast<char *>(addr), reinterpret_cast<char *>(addr) + len);
    mprotect(p.start, p.len, PROT_READ | PROT_EXEC); // restore .text protection
    return true;
}

} // namespace pal
