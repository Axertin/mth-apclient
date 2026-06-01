#include "mth/core/app.hpp"

#include "mth/core/build_id.hpp"
#include "pal/pal_hook.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

#include "mth_version.h"

namespace mth
{

App::App()
{
    pal::logf(pal::LogLevel::Info, "mth-apclient %.*s loaded", static_cast<int>(version::string.size()), version::string.data());

    const auto game = pal::game_module();
    const auto self = pal::self_module();
    const auto bid = pal::game_build_id();
    const auto build = detect_build(bid);
    const auto name = build_name(build);

    pal::logf(pal::LogLevel::Info, "game base=0x%llx size=0x%zx path=%s", static_cast<unsigned long long>(game.base), game.size, game.path.c_str());
    pal::logf(pal::LogLevel::Info, "self base=0x%llx path=%s", static_cast<unsigned long long>(self.base), self.path.c_str());
    pal::logf(pal::LogLevel::Info, "build_id=%s build=%.*s", bid.c_str(), static_cast<int>(name.size()), name.data());

    pal::init_hook_engine();
}

App::~App()
{
    pal::shutdown_hook_engine();
    pal::logf(pal::LogLevel::Info, "mth-apclient unloading");
}

void App::run()
{
    // Base scaffold: no game hooks, no networking yet. Proves the pipeline:
    // inject -> log -> bring up the hook backend -> idle cleanly.
    pal::logf(pal::LogLevel::Info, "App::run -- idle (base scaffold; no hooks installed yet)");
}

} // namespace mth
