#include "MinaModAPI.h"
#include "mod/mod_api.hpp"
#include "pal/pal_crash.hpp"
#include "pal/pal_entry.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_thread.hpp"

namespace
{

void apclient_main_trampoline(void * /*arg*/)
{
    pal::apclient_main();
}

} // namespace

// Entry point the native mod loader calls. MM_EXPORT carries the extern "C"
// linkage and the default-visibility attribute the hidden-by-default build needs.
MM_EXPORT void MinaMod_Init(MinaModAPI *mm)
{
    mod::set_api(mm);
    pal::log_init();
    pal::install_crash_handler(); // before any mod work, so it catches everything
    if (mm && mm->APIVersion != MinaModAPI_Version)
        pal::logf(pal::LogLevel::Warn, "MinaModAPI version mismatch (expected %u, got %zu); proceeding anyway", static_cast<unsigned>(MinaModAPI_Version),
                  static_cast<size_t>(mm->APIVersion));
    pal::spawn_thread("mthap-main", &apclient_main_trampoline, nullptr);
}
