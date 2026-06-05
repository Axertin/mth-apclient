#include "pal/pal_thread.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <cwchar>

#include <processthreadsapi.h>

namespace
{

struct TrampolineArg
{
    pal::ThreadFn fn;
    void *user;
    char name[32];
};

DWORD WINAPI trampoline(LPVOID raw)
{
    auto *arg = static_cast<TrampolineArg *>(raw);

    wchar_t wname[32] = {}; // SetThreadDescription requires wchar_t
    for (int i = 0; i < 31 && arg->name[i]; ++i)
        wname[i] = static_cast<wchar_t>(static_cast<unsigned char>(arg->name[i]));
    SetThreadDescription(GetCurrentThread(), wname);

    pal::ThreadFn fn = arg->fn;
    void *user = arg->user;
    delete arg;
    fn(user);
    return 0;
}

} // namespace

namespace pal
{

void spawn_thread(const char *name, ThreadFn fn, void *arg)
{
    auto *wrap = new TrampolineArg{fn, arg, {}};
    if (name)
    {
        std::strncpy(wrap->name, name, sizeof(wrap->name) - 1);
    }
    HANDLE h = CreateThread(nullptr, 0, &trampoline, wrap, 0, nullptr);
    if (h)
        CloseHandle(h);
}

} // namespace pal
