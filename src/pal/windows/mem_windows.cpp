#include "pal/pal_mem.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace pal
{

bool make_writable(void *addr, std::size_t len)
{
    DWORD old = 0;
    return VirtualProtect(addr, len, PAGE_READWRITE, &old) != 0;
}

} // namespace pal
