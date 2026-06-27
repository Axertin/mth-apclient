#include <atomic>

#include "MinaModAPI.h"
#include "pal/pal_crash.hpp"
#include "pal/pal_entry.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_native_mod.hpp"
#include "pal/pal_thread.hpp"

#if defined(_WIN32)
#define MM_EXPORT __declspec(dllexport)
#else
#define MM_EXPORT __attribute__((visibility("default")))
#endif

namespace
{

MinaModAPI *g_mod_api = nullptr;
std::atomic<void (*)()> g_fixed_update_handler{nullptr};

// Game-thread tick from the native hook; no-op until App registers a handler.
void on_fixed_update(void * /*pCtx*/)
{
    if (auto handler = g_fixed_update_handler.load(std::memory_order_relaxed))
        handler();
}

void apclient_main_trampoline(void * /*arg*/)
{
    pal::apclient_main();
}

} // namespace

namespace pal
{

void set_fixed_update_handler(void (*handler)())
{
    g_fixed_update_handler.store(handler, std::memory_order_relaxed);
}

} // namespace pal

// Entry point the native mod loader calls.
extern "C" MM_EXPORT void MinaMod_Init(MinaModAPI *mm)
{
    g_mod_api = mm;
    pal::log_init();
    pal::install_crash_handler(); // before any mod work, so it catches everything
    if (mm && mm->APIVersion != MinaModAPI_Version)
        pal::logf(pal::LogLevel::Warn, "MinaModAPI version mismatch (expected %u, got %zu); proceeding anyway", static_cast<unsigned>(MinaModAPI_Version),
                  static_cast<size_t>(mm->APIVersion));
    if (mm && mm->InstallHook)
        mm->InstallHook("FixedUpdate", 0, &on_fixed_update);
    pal::spawn_thread("mthap-main", &apclient_main_trampoline, nullptr);
}
