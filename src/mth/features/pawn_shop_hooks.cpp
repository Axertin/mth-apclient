#include "mth/features/pawn_shop_hooks.hpp"

#include <utility>

#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"

namespace mth
{

PawnShopHooks::PawnShopHooks(std::function<bool()> should_disable)
{
    if (!pal::install_pawn_shop_hook(std::move(should_disable)))
        pal::logf(pal::LogLevel::Warn, "PawnShopHooks: pawn-shop chokepoint unresolved; disable inactive");
}

PawnShopHooks::~PawnShopHooks()
{
    pal::remove_pawn_shop_hook();
}

} // namespace mth
