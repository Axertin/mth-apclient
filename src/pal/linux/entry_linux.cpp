#include <climits>
#include <cstdlib>
#include <cstring>

#include <unistd.h>

#include "pal/pal_entry.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_thread.hpp"

namespace
{

// Steam inherits LD_PRELOAD into helper subprocesses; guard on executable basename.
bool is_target_process()
{
    if (const char *force = std::getenv("MTHAP_FORCE_INIT"); force && *force)
        return true;
    char path[PATH_MAX + 1];
    const ssize_t n = readlink("/proc/self/exe", path, PATH_MAX);
    if (n <= 0)
        return false;
    path[n] = '\0';
    const char *slash = std::strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    return std::strcmp(base, "MinaTheHollower") == 0;
}

void apclient_main_trampoline(void * /*arg*/)
{
    pal::apclient_main();
}

__attribute__((constructor(65535))) void mthap_init()
{
    if (!is_target_process())
        return;
    pal::log_init();
    pal::spawn_thread("mthap-main", &apclient_main_trampoline, nullptr);
}

__attribute__((destructor)) void mthap_fini()
{
    pal::log_shutdown();
}

} // namespace
