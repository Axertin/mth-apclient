#include "mth/core/app.hpp"
#include "pal/pal_entry.hpp"

namespace
{

// Intentionally leaked: destroying Frida detours at exit is a crash footgun.
mth::App *g_app = nullptr;

} // namespace

namespace pal
{

void apclient_main()
{
    g_app = new mth::App(); // leaked by design
    g_app->run();
}

} // namespace pal
