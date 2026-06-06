#include <cstdint>

#include <sys/mman.h>
#include <unistd.h>

#include "pal/pal_mem.hpp"

namespace pal
{

bool make_writable(void *addr, std::size_t len)
{
    const long ps = sysconf(_SC_PAGESIZE);
    const auto a = reinterpret_cast<std::uintptr_t>(addr);
    const std::uintptr_t start = a & ~static_cast<std::uintptr_t>(ps - 1);
    const std::uintptr_t end = (a + len + ps - 1) & ~static_cast<std::uintptr_t>(ps - 1);
    return mprotect(reinterpret_cast<void *>(start), end - start, PROT_READ | PROT_WRITE) == 0;
}

} // namespace pal
