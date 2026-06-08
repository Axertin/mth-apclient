#include <cstddef>
#include <cstdint>

#include "mth/core/game_symbols.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_hook.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

namespace
{
struct Vec3
{
    float x, y, z;
};
Vec3 (*g_get_pos)(const void *) = nullptr; // Itanium returns the vec in registers

// ShopMenu field offsets (build-specific; the AP callback self-checks the read locIdx/itemType).
constexpr std::ptrdiff_t kShopLocIdxOff = 0x218;
constexpr std::ptrdiff_t kShopItemTypeOff = 0x21c;

pal::ShopBuyFn g_on_shop_buy = nullptr;
pal::HookId g_shop_hook = pal::kInvalidHookId;
void (*g_orig_item_present)(void *) = nullptr;

// ItemPresent runs once when a shop item is granted; read locIdx/itemType, let the AP callback
// collect it, and write back the (possibly redirected) itemType BEFORE the original grants.
void repl_item_present(void *self)
{
    if (g_on_shop_buy != nullptr && self != nullptr)
    {
        int &loc_idx = *reinterpret_cast<int *>(static_cast<char *>(self) + kShopLocIdxOff);
        int &item_type = *reinterpret_cast<int *>(static_cast<char *>(self) + kShopItemTypeOff);
        pal::logf(pal::LogLevel::Debug, "shop: ItemPresent locIdx=%d itemType=%d", loc_idx, item_type);
        item_type = g_on_shop_buy(loc_idx, item_type); // redirect suppresses the vanilla grant
    }
    if (g_orig_item_present)
        g_orig_item_present(self);
}
} // namespace

namespace pal
{

bool read_player_position(const void *trackable, float out[3])
{
    if (g_get_pos == nullptr)
        g_get_pos = reinterpret_cast<Vec3 (*)(const void *)>(resolve_game_symbol(mth::sym::player_trackable_get_pos));
    if (g_get_pos == nullptr || trackable == nullptr)
        return false;
    const Vec3 v = g_get_pos(trackable);
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
    return true;
}

void *pickup_base_from_onpickup(void *onpickup_this)
{
    return onpickup_this; // Itanium routes the secondary-base adjust through a separate _ZThn thunk
}

void *active_save_slot(std::uintptr_t save_manager_global)
{
    if (save_manager_global == 0)
        return nullptr;
    return *reinterpret_cast<void **>(save_manager_global + 0x18); // SaveSlot* = *(g_saveManager + 0x18)
}

bool install_shop_purchase_hook(ShopBuyFn on_buy)
{
    g_on_shop_buy = on_buy;
    const std::uintptr_t addr = resolve_game_symbol(mth::sym::shop_item_present);
    if (addr == 0)
    {
        logf(LogLevel::Warn, "shop: ShopMenu::ItemPresent not resolved; shop check disabled");
        return false;
    }
    g_shop_hook = hook_engine().install_hook(reinterpret_cast<void *>(addr), reinterpret_cast<void *>(&repl_item_present),
                                             reinterpret_cast<void **>(&g_orig_item_present));
    if (g_shop_hook == kInvalidHookId)
    {
        logf(LogLevel::Error, "shop: failed to hook ShopMenu::ItemPresent");
        return false;
    }
    logf(LogLevel::Info, "shop: hooked ShopMenu::ItemPresent (id=%llu)", static_cast<unsigned long long>(g_shop_hook));
    return true;
}

void remove_shop_purchase_hook()
{
    if (g_shop_hook != kInvalidHookId)
        hook_engine().remove_hook(g_shop_hook);
    g_shop_hook = kInvalidHookId;
    g_on_shop_buy = nullptr;
}

} // namespace pal
