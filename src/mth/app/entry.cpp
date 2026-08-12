#include "mth/app/app.hpp"
#include "pal/pal_entry.hpp"

namespace
{

// Intentionally leaked, and the mod exports no unload path: neither hook backend drains threads already
// inside a detour, so reverting one while the game runs is a crash footgun. So ~App and everything it
// tears down (HookManager, the feature hooks, their hook removals) never run today. They stay correct for
// whatever does destroy an App later; unreachable is not the same as unneeded.
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
