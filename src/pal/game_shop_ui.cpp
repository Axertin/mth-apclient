// ShopMenu instance accessors plus the two shop detours whose bodies are identical on both builds: the
// box-list walk behind the AP location scan, the SetCursor text post-hook, and the Shop::Get flatten. The
// purchase detour stays in the platform files, where the buy-confirm frame offsets differ per build.

#include <cstddef>
#include <cstdint>

#include "mod/mod_api.hpp"
#include "mth/core/data/game_layout.hpp"
#include "mth/core/data/game_symbols.hpp"
#include "mth/core/shop_boxes.hpp"
#include "mth/core/shop_flatten.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_hook.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_mem.hpp"
#include "pal/pal_module.hpp"

namespace
{

// ShopItem / ShopItemDef instance offsets. Verified on the Windows depot build: ShopItem+0xf8 (active
// def*), +0xec (stock count), ShopItemDef+0x48 (cached GetCollectionIndex == loc_idx) all match Linux.

pal::ShopFlattenFn g_shop_flatten_cb = nullptr;
pal::HookId g_shop_flatten_hook = pal::kInvalidHookId;
// Shop::Get takes a 64-bit name hash. Use a fixed-width type: `unsigned long` is 32-bit under Windows
// LLP64 and would truncate the hash, so the forwarded lookup finds no shop and returns null.
void *(*g_orig_shop_get)(std::uint64_t name_hash) = nullptr; // Shop::Get(uint64_t) -> ShopDef*

// Shop::Get(nameHash) is the accessor InteractComponent::OpenShop consults before building a shop's
// box list; OR the never-stack bit onto the returned ShopDef so stacked slots flatten (one box/level).
void *repl_shop_get(std::uint64_t name_hash)
{
    void *def = g_orig_shop_get != nullptr ? g_orig_shop_get(name_hash) : nullptr;
    if (def != nullptr && g_shop_flatten_cb != nullptr && g_shop_flatten_cb())
    {
        auto *flags = reinterpret_cast<std::uint32_t *>(static_cast<char *>(def) + mth::kShopFlagsOff);
        *flags = mth::apply_flatten_flag(*flags, true);
    }
    return def;
}

// ShopMenu::SetCursor post-hook: rewrites the selected box's name+description text from scouted AP data.
// Standalone on Windows (not inlined), so this is a direct post-hook exactly like Linux.
pal::ShopTextFn g_shop_text_cb = nullptr;
pal::HookId g_shop_text_hook = pal::kInvalidHookId;
void (*g_orig_set_cursor)(void *, int, bool) = nullptr;

// ShopMenu instance fields (box list + selection); distinct from the InitState frame offsets above.
// Confirmed identical to Linux by decompiling SetCursor on the r148851 PE.
constexpr std::ptrdiff_t kShopNameWidgetOff = 0x148;
constexpr std::ptrdiff_t kShopDescWidgetOff = 0x150;
constexpr std::ptrdiff_t kShopBoxArrayOff = 0x1c8;
constexpr std::ptrdiff_t kShopBoxCountOff = 0x1d0;
constexpr std::ptrdiff_t kShopCursorOff = 0x1d8;
constexpr std::ptrdiff_t kShopModeOff = 0x23c;
constexpr std::ptrdiff_t kShopBoxItemTypeOff = 0xcc; // ShopItem -> itemType (box's item kind)
constexpr int kShopSkipItemType = 0x65;

// ShopMenu runs in one of two modes, selected by the int at +0x23c: the ctor zeroes it, then stores 1 for
// the shop named `Color` (the Atelier's "Custom Fitting" appearance menu). A mode-1 SetupBoxes takes the
// palette-widget branch and returns without ever filling the box array. Gate on != 0 rather than the
// game's own == 1 so that a mode added by a later build is excluded by default.
[[nodiscard]] bool shop_menu_is_stocked(const void *shop_menu) noexcept
{
    return *reinterpret_cast<const int *>(static_cast<const char *>(shop_menu) + kShopModeOff) == 0;
}

// Resolve the box array plus a count that is safe to walk, or false when this menu has no walkable rows.
// OpenShop allocates +0x1c8 and sets a real row count in +0x1d0 for every menu including a mode-1 one,
// but SetupBoxes returns before filling that array, leaving a live, correctly sized buffer full of
// uninitialized heap. A null check and a range check both pass on it; only the mode gate catches it.
[[nodiscard]] bool shop_box_list(void *shop_menu, void ***out_boxes, int *out_count) noexcept
{
    if (!pal::pointer_looks_valid(shop_menu) || !shop_menu_is_stocked(shop_menu))
        return false;
    void **boxes = *reinterpret_cast<void ***>(static_cast<char *>(shop_menu) + kShopBoxArrayOff);
    const int count = mth::shop_box_walk_count(*reinterpret_cast<int *>(static_cast<char *>(shop_menu) + kShopBoxCountOff));
    if (!pal::pointer_looks_valid(boxes) || count <= 0)
        return false;
    *out_boxes = boxes;
    *out_count = count;
    return true;
}

void repl_set_cursor(void *self, int index, bool b)
{
    if (g_orig_set_cursor)
        g_orig_set_cursor(self, index, b);
    if (g_shop_text_cb != nullptr && self != nullptr)
        g_shop_text_cb(self);
}

} // namespace

namespace pal
{

bool install_shop_flatten_hook(ShopFlattenFn active)
{
    g_shop_flatten_cb = active;
    const std::uintptr_t addr = resolve_game_symbol(mth::sym::shop_get);
    if (addr == 0)
    {
        logf(LogLevel::Warn, "shop: Shop::Get not resolved; stacked-shop flattening disabled");
        g_shop_flatten_cb = nullptr;
        return false;
    }
    g_shop_flatten_hook =
        hook_engine().install_hook(reinterpret_cast<void *>(addr), reinterpret_cast<void *>(&repl_shop_get), reinterpret_cast<void **>(&g_orig_shop_get));
    if (g_shop_flatten_hook == kInvalidHookId)
    {
        logf(LogLevel::Error, "shop: failed to hook Shop::Get");
        g_shop_flatten_cb = nullptr;
        return false;
    }
    logf(LogLevel::Info, "shop: hooked Shop::Get for flattening (id=%llu)", static_cast<unsigned long long>(g_shop_flatten_hook));
    return true;
}

void remove_shop_flatten_hook()
{
    if (g_shop_flatten_hook != kInvalidHookId)
        hook_engine().remove_hook(g_shop_flatten_hook);
    g_shop_flatten_hook = kInvalidHookId;
    g_shop_flatten_cb = nullptr;
    g_orig_shop_get = nullptr;
}

int shop_selected_loc(void *shop_menu)
{
    void **boxes = nullptr;
    int count = 0;
    if (!shop_box_list(shop_menu, &boxes, &count))
        return -1;
    const int cursor = *reinterpret_cast<int *>(static_cast<char *>(shop_menu) + kShopCursorOff);
    if (cursor < 0 || cursor >= count)
        return -1;
    void *box = boxes[cursor];
    if (!pal::pointer_looks_valid(box))
        return -1;
    const int item_type = *reinterpret_cast<int *>(static_cast<char *>(box) + kShopBoxItemTypeOff);
    if (item_type == kShopSkipItemType)
        return -1;
    // Sold-out box: stock count 0 (same field the ShopItem::Refresh hook zeroes for a fully-bought slot).
    if (*reinterpret_cast<int *>(static_cast<char *>(box) + mth::layout::kShopItemStockOff) == 0)
        return -1;
    void *def = *reinterpret_cast<void **>(static_cast<char *>(box) + mth::layout::kShopItemDefOff);
    if (!pal::pointer_looks_valid(def))
        return -1;
    return *reinterpret_cast<int *>(static_cast<char *>(def) + mth::layout::kShopDefLocOff);
}

void shop_enumerate_locs(void *shop_menu, void (*sink)(int loc, void *ctx), void *ctx)
{
    void **boxes = nullptr;
    int count = 0;
    if (sink == nullptr || !shop_box_list(shop_menu, &boxes, &count))
        return;
    for (int i = 0; i < count; ++i)
    {
        void *box = boxes[i];
        if (!pal::pointer_looks_valid(box))
            continue;
        void *def = *reinterpret_cast<void **>(static_cast<char *>(box) + mth::layout::kShopItemDefOff);
        if (!pal::pointer_looks_valid(def))
            continue;
        sink(*reinterpret_cast<int *>(static_cast<char *>(def) + mth::layout::kShopDefLocOff), ctx);
    }
}

// Deliberately not mode-gated: SetupBoxes builds a live text widget for the appearance menu too, so these
// return a real widget there. Safe only because on_shop_set_cursor resolves a location first and bails
// when there is none; keep that order, or the appearance menu's title gets overwritten with an AP name.
void *shop_name_widget(void *shop_menu)
{
    return shop_menu != nullptr ? *reinterpret_cast<void **>(static_cast<char *>(shop_menu) + kShopNameWidgetOff) : nullptr;
}

void *shop_desc_widget(void *shop_menu)
{
    return shop_menu != nullptr ? *reinterpret_cast<void **>(static_cast<char *>(shop_menu) + kShopDescWidgetOff) : nullptr;
}

void shop_set_text(void *widget, const char *utf8)
{
    mod::set_text(widget, utf8);
}

bool install_shop_text_hook(ShopTextFn on_set_cursor)
{
    const std::uintptr_t addr = resolve_game_symbol(mth::sym::shop_set_cursor);
    if (addr == 0)
    {
        logf(LogLevel::Warn, "shop: ShopMenu::SetCursor not resolved; shop text override disabled");
        return false;
    }
    g_shop_text_cb = on_set_cursor;
    g_shop_text_hook =
        hook_engine().install_hook(reinterpret_cast<void *>(addr), reinterpret_cast<void *>(&repl_set_cursor), reinterpret_cast<void **>(&g_orig_set_cursor));
    if (g_shop_text_hook == kInvalidHookId)
    {
        logf(LogLevel::Error, "shop: failed to hook ShopMenu::SetCursor");
        g_shop_text_cb = nullptr;
        return false;
    }
    logf(LogLevel::Info, "shop: hooked ShopMenu::SetCursor for text override (id=%llu)", static_cast<unsigned long long>(g_shop_text_hook));
    return true;
}

void remove_shop_text_hook()
{
    if (g_shop_text_hook != kInvalidHookId)
        hook_engine().remove_hook(g_shop_text_hook);
    g_shop_text_hook = kInvalidHookId;
    g_shop_text_cb = nullptr;
    g_orig_set_cursor = nullptr;
}

} // namespace pal
