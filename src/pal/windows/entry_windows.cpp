#include <cstdlib>
#include <cstring>

#include "pal/pal_entry.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_thread.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace mth
{
// Defined in proxy_version.cpp. Loads the real System32\version.dll so the
// proxy exports can forward to it. Called synchronously on DLL attach because
// the host process's version.dll imports resolve through us immediately.
void proxy_version_load_real();
} // namespace mth

namespace
{

// The mod may be loaded by more than just the game (the depot also ships
// handler.exe). Only bootstrap inside MinaTheHollower.exe. MTHAP_FORCE_INIT=1
// bypasses the check.
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
    // Let the loader lock release before we touch anything that might load
    // further modules (the hook backend init will).
    Sleep(100);
    pal::apclient_main();
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE hInstDll, DWORD reason, LPVOID /*reserved*/)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hInstDll);
        // Always stand up the real version.dll first - we forward its exports
        // for whatever process loaded us, even if we don't bootstrap the mod here.
        mth::proxy_version_load_real();
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
