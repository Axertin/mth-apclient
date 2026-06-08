#include <cstddef>
#include <cstdint>

#include "mth/core/game_symbols.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_hook.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

namespace
{
// ShopMenu::InitState offsets: ItemPresent is inlined into InitState's buy-confirm state (7).
constexpr std::ptrdiff_t kShopStateOff = 0x2c; // int: state machine; 7 = buy-confirm
constexpr int kShopBuyConfirmState = 7;
constexpr std::ptrdiff_t kShopFlagsObjOff = 0x178; // -> obj; flags at obj+0x228
constexpr std::ptrdiff_t kShopFlagsOff = 0x228;
constexpr std::ptrdiff_t kShopGateByteOff = 0x22b; // char: purchase-commit gate
constexpr std::ptrdiff_t kShopLocIdxOff = 0x1e0;   // int
constexpr std::ptrdiff_t kShopItemTypeOff = 0x1e4; // int

pal::ShopBuyFn g_on_shop_buy = nullptr;
pal::HookId g_shop_hook = pal::kInvalidHookId;
void (*g_orig_init_state)(void *) = nullptr;
int g_shop_last_locidx = -1; // dedup: InitState is re-entered while in the buy-confirm state

// Run the original FIRST: in state 7 the game writes locIdx/itemType partway through InitState, so
// they are only valid post-init. The game's own sentinel (saveslot+0xc70/+0xc74) suppresses the
// vanilla grant, so we do NOT redirect itemType here.
void repl_init_state(void *self)
{
    if (g_orig_init_state)
        g_orig_init_state(self);

    if (g_on_shop_buy != nullptr && self != nullptr)
    {
        const int state = *reinterpret_cast<int *>(static_cast<char *>(self) + kShopStateOff);
        if (state == kShopBuyConfirmState)
        {
            void *flags_obj = *reinterpret_cast<void **>(static_cast<char *>(self) + kShopFlagsObjOff);
            const unsigned flags = flags_obj != nullptr ? *reinterpret_cast<unsigned *>(static_cast<char *>(flags_obj) + kShopFlagsOff) : 0xFFFFFFFFu;
            const char gate = *reinterpret_cast<char *>(static_cast<char *>(self) + kShopGateByteOff);
            const int loc_idx = *reinterpret_cast<int *>(static_cast<char *>(self) + kShopLocIdxOff);
            const int item_type = *reinterpret_cast<int *>(static_cast<char *>(self) + kShopItemTypeOff);

            // Informational: surfaces the bought slot for serverless testing.
            pal::logf(pal::LogLevel::Debug, "shop: buy-confirm locIdx=%d itemType=%d flags=0x%x gate=%d", loc_idx, item_type, flags, static_cast<int>(gate));

            // Replicate ItemPresent's guards: only on a real, committed buy.
            if (flags_obj != nullptr && (flags & 0x4u) == 0 && (flags & 0x400u) == 0 && (flags & 0x1000u) == 0 && gate != 0 && loc_idx != g_shop_last_locidx)
            {
                g_shop_last_locidx = loc_idx;      // dedup this confirmed buy
                g_on_shop_buy(loc_idx, item_type); // return ignored: no itemType redirect on Windows
            }
        }
        else
        {
            g_shop_last_locidx = -1; // left the buy state; allow the next buy
        }
    }
}
} // namespace

namespace pal
{

// Field offsets for build 6a23742f; re-derive on a game update (a wrong offset yields a bad
// position, not a crash). Selector at +0x74 picks between the two base-position sources.
bool read_player_position(const void *trackable, float out[3])
{
    if (trackable == nullptr)
        return false;
    const char *t = static_cast<const char *>(trackable);
    const auto f = [t](std::ptrdiff_t o) { return *reinterpret_cast<const float *>(t + o); };
    if (*reinterpret_cast<const unsigned char *>(t + 0x74) != 0)
    {
        out[0] = f(0x68);
        out[1] = f(0x6c);
        out[2] = f(0x70);
    }
    else
    {
        out[0] = f(0x50) + f(0x5c);
        out[1] = f(0x54) + f(0x60);
        out[2] = f(0x58) + f(0x64);
    }
    return true;
}

void *pickup_base_from_onpickup(void *onpickup_this)
{
    return static_cast<char *>(onpickup_this) - 0x180; // PickupListener subobject offset, build 6a23742f
}

void *active_save_slot(std::uintptr_t save_manager_global)
{
    if (save_manager_global == 0)
        return nullptr;
    return *reinterpret_cast<void **>(save_manager_global); // the global already holds the active SaveSlot*
}

bool install_shop_purchase_hook(ShopBuyFn on_buy)
{
    g_on_shop_buy = on_buy;
    const std::uintptr_t addr = resolve_game_symbol(mth::sym::shop_init_state);
    if (addr == 0)
    {
        logf(LogLevel::Warn, "shop: ShopMenu::InitState not resolved; shop check disabled");
        return false;
    }
    g_shop_hook =
        hook_engine().install_hook(reinterpret_cast<void *>(addr), reinterpret_cast<void *>(&repl_init_state), reinterpret_cast<void **>(&g_orig_init_state));
    if (g_shop_hook == kInvalidHookId)
    {
        logf(LogLevel::Error, "shop: failed to hook ShopMenu::InitState");
        return false;
    }
    logf(LogLevel::Info, "shop: hooked ShopMenu::InitState (id=%llu)", static_cast<unsigned long long>(g_shop_hook));
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
