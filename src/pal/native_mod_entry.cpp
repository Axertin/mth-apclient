#include "MinaModAPI.h"
#include "mod/mod_api.hpp"
#include "pal/pal_crash.hpp"
#include "pal/pal_entry.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_thread.hpp"

#if defined(_WIN32)
#define MM_EXPORT __declspec(dllexport)
#else
#define MM_EXPORT __attribute__((visibility("default")))
#endif

namespace
{

void apclient_main_trampoline(void * /*arg*/)
{
    pal::apclient_main();
}

} // namespace

// Entry point the native mod loader calls.
extern "C" MM_EXPORT void MinaMod_Init(MinaModAPI *mm)
{
    mod::set_api(mm);
    pal::log_init();
    pal::install_crash_handler(); // before any mod work, so it catches everything
    if (mm && mm->APIVersion != MinaModAPI_Version)
        pal::logf(pal::LogLevel::Warn, "MinaModAPI version mismatch (expected %u, got %zu); proceeding anyway", static_cast<unsigned>(MinaModAPI_Version),
                  static_cast<size_t>(mm->APIVersion));
    pal::spawn_thread("mthap-main", &apclient_main_trampoline, nullptr);
}
