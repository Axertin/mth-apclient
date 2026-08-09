#include "mth/features/sewer_cat_hooks.hpp"

#include <utility>

#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"

namespace mth
{

SewerCatHooks::SewerCatHooks(std::function<bool()> should_disable)
{
    if (!pal::install_sewer_cat_hook(std::move(should_disable)))
        pal::logf(pal::LogLevel::Warn, "SewerCatHooks: fetch-vendor chokepoint unresolved; disable inactive");
}

SewerCatHooks::~SewerCatHooks()
{
    pal::remove_sewer_cat_hook();
}

} // namespace mth
