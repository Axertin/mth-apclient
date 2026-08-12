#include "mth/app/app.hpp"
#include "pal/pal_entry.hpp"

namespace
{

// Intentionally leaked, and the mod exports no unload path: neither hook backend drains threads already
// inside a detour, so reverting one while the game runs is a crash footgun. ~App and its teardown chain
// (HookManager, the feature hooks, their hook removals) is kept correct but never runs today.
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
