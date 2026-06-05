#include <cstdlib>
#include <cstring>

#include "pal/pal_entry.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_thread.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace mth
{
void proxy_version_load_real();
} // namespace mth

namespace
{

// The depot also ships handler.exe; guard on executable basename.
bool is_target_process()
{
    if (const char *force = std::getenv("MTHAP_FORCE_INIT"); force && *force)
        return true;
    char path[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return false;
    const char *slash = std::strrchr(path, '\\');
    const char *base = slash ? slash + 1 : path;
    return _stricmp(base, "MinaTheHollower.exe") == 0;
}

void apclient_main_trampoline(void * /*arg*/)
{
    Sleep(100); // release loader lock before hook backend init loads further modules
    pal::apclient_main();
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE hInstDll, DWORD reason, LPVOID /*reserved*/)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hInstDll);
        mth::proxy_version_load_real(); // must run for every process, not just the game
        if (!is_target_process())
            break;
        pal::log_init();
        pal::spawn_thread("mthap-main", &apclient_main_trampoline, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        pal::log_shutdown();
        break;
    default:
        break;
    }
    return TRUE;
}
