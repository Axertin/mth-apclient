#include <cstring>

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

bool patch_code(void *addr, const void *bytes, std::size_t len)
{
    DWORD old = 0;
    if (!VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &old))
        return false;
    std::memcpy(addr, bytes, len);
    VirtualProtect(addr, len, old, &old); // restore original (executable) protection
    FlushInstructionCache(GetCurrentProcess(), addr, len);
    return true;
}

} // namespace pal
