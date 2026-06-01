#include "mth/core/app.hpp"
#include "pal/pal_entry.hpp"

namespace
{

// The one unavoidable global: the composition root. Created on the worker
// thread by apclient_main and **intentionally leaked** — it lives for the
// process lifetime so the installed tick hooks stay active while the game runs.
// We deliberately do NOT destroy it at exit: reverting Frida detours during
// process teardown is a crash footgun, and the OS reclaims everything anyway.
mth::App *g_app = nullptr;

} // namespace

namespace pal
{

void apclient_main()
{
    g_app = new mth::App(); // leaked by design — see above
    g_app->run();
}

} // namespace pal
