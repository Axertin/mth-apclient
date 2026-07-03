#include <cstdio>
#include <string>

#include "pal/pal_module.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace
{

std::string wide_to_utf8(const wchar_t *w)
{
    if (!w || !*w)
        return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1)
        return {};
    std::string s(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
    return s;
}

pal::ModuleInfo module_from_handle(HMODULE h)
{
    pal::ModuleInfo info{};
    if (!h)
        return info;

    info.base = reinterpret_cast<std::uintptr_t>(h);

    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(h, buf, MAX_PATH);
    if (n && n < MAX_PATH)
        info.path = wide_to_utf8(buf);

    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(h);
    if (dos && dos->e_magic == IMAGE_DOS_SIGNATURE)
    {
        const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(reinterpret_cast<const BYTE *>(h) + dos->e_lfanew);
        if (nt->Signature == IMAGE_NT_SIGNATURE)
        {
            info.size = nt->OptionalHeader.SizeOfImage;
        }
    }
    return info;
}

} // namespace

namespace pal
{

ModuleInfo game_module()
{
    return module_from_handle(GetModuleHandleW(nullptr));
}

ModuleInfo self_module()
{
    HMODULE h = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&self_module), &h);
    return module_from_handle(h);
}

std::uintptr_t rva(std::uintptr_t relative)
{
    return game_module().base + relative;
}

void *find_symbol(const char *module_basename, const char *symbol)
{
    if (!symbol)
        return nullptr;
    HMODULE h = module_basename ? GetModuleHandleA(module_basename) : GetModuleHandleW(nullptr);
    if (!h)
        return nullptr;
    return reinterpret_cast<void *>(GetProcAddress(h, symbol));
}

} // namespace pal
