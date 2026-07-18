#include "mth/features/fountain_lamp_hooks.hpp"

#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"

namespace mth
{

FountainLampHooks::FountainLampHooks()
{
    if (!pal::install_fountain_lamp_hook([this] { return mask_.load(std::memory_order_relaxed); }))
        pal::logf(pal::LogLevel::Warn, "FountainLampHooks: HubFountain::Bulb::Update unresolved; lamp pre-light inactive");
}

FountainLampHooks::~FountainLampHooks()
{
    pal::remove_fountain_lamp_hook();
}

} // namespace mth
