#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

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
constexpr std::ptrdiff_t kShopFlagsObjOff = 0x178; // -> obj; flags at obj+0x228 (bit 0x4 = pawn-sell)
constexpr std::ptrdiff_t kShopFlagsOff = 0x228;
constexpr std::ptrdiff_t kShopLocIdxOff = 0x1e0;   // int
constexpr std::ptrdiff_t kShopItemTypeOff = 0x1e4; // int

pal::ShopBuyFn g_on_shop_buy = nullptr;
pal::HookId g_shop_hook = pal::kInvalidHookId;
void (*g_orig_init_state)(void *) = nullptr;
int g_shop_last_locidx = -1; // dedup: InitState is re-entered while in the buy-confirm state

// Run the original FIRST: state 7 writes locIdx/itemType partway through InitState, so they are only
// valid post-init. The grant has already run by then, so the vanilla item can't be redirected here; it
// is instead suppressed in the Items::OnPickupDone detour (which skips real grants at AP locations).
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
            const int loc_idx = *reinterpret_cast<int *>(static_cast<char *>(self) + kShopLocIdxOff);
            const int item_type = *reinterpret_cast<int *>(static_cast<char *>(self) + kShopItemTypeOff);

            // Informational: surfaces the bought slot for serverless testing.
            pal::logf(pal::LogLevel::Debug, "shop: buy-confirm locIdx=%d itemType=%d flags=0x%x", loc_idx, item_type, flags);

            // Entering state 7 is the confirmed-buy grant (deducts money + grants).
            // Skip pawn-sells (flags bit 0x4) and dedup the per-frame InitState re-entries.
            if ((flags & 0x4u) == 0 && loc_idx != g_shop_last_locidx)
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

// ShopItem / ShopItemDef instance offsets. Verified on the Windows depot build: ShopItem+0xf8 (active
// def*), +0xec (stock count), ShopItemDef+0x48 (cached GetCollectionIndex == loc_idx), +0x28 (next
// level variant) all match Linux.
constexpr std::ptrdiff_t kShopItemDefOff = 0xf8;   // ShopItem -> active ShopItemDef*
constexpr std::ptrdiff_t kShopItemStockOff = 0xec; // ShopItem stock count; 0 renders the "sold out" box
constexpr std::ptrdiff_t kShopDefLocOff = 0x48;    // ShopItemDef cached GetCollectionIndex == loc_idx
constexpr std::ptrdiff_t kShopDefNextOff = 0x28;   // ShopItemDef -> next variant (level chain), null-terminated

pal::ShopLevelFn g_shop_stock_cb = nullptr;
pal::HookId g_shop_stock_hook = pal::kInvalidHookId;
void (*g_orig_shop_refresh)(void *) = nullptr;

// A shop slot is a chain of ShopItemDef variants (one per level; rising price), linked via +0x28, each
// with its own loc_idx at +0x48. Vanilla advances past bought levels and sells out via the suppressed
// grant, so for AP slots we replicate it from AP state: walk from the active variant, advance to the
// first level not yet checked, set the stock count (ShopItem+0xec) to the number of unchecked AP levels
// (so the displayed quantity is right), and let it reach 0 only when every level is checked (sold out).
// A slot with no AP-location levels (normal items, consumables) is left entirely untouched.
void repl_shop_refresh(void *self)
{
    void *def = self != nullptr ? *reinterpret_cast<void **>(static_cast<char *>(self) + kShopItemDefOff) : nullptr;
    if (g_shop_stock_cb != nullptr && def != nullptr)
    {
        void *first_unbought = nullptr;
        int remaining = 0; // AP levels not yet checked
        bool any_ap = false;
        for (void *v = def; v != nullptr; v = *reinterpret_cast<void **>(static_cast<char *>(v) + kShopDefNextOff))
        {
            const int loc_idx = *reinterpret_cast<int *>(static_cast<char *>(v) + kShopDefLocOff);
            const int state = loc_idx >= 0 ? g_shop_stock_cb(loc_idx) : 0; // 0 not-AP, 1 unchecked, 2 checked
            if (state != 0)
                any_ap = true;
            if (state != 2) // not a bought level
            {
                if (first_unbought == nullptr)
                    first_unbought = v;
                if (state == 1)
                    ++remaining;
            }
        }
        if (first_unbought == nullptr)
            *reinterpret_cast<int *>(static_cast<char *>(self) + kShopItemStockOff) = 0; // all levels bought -> sold out
        else if (any_ap)
        {
            *reinterpret_cast<void **>(static_cast<char *>(self) + kShopItemDefOff) = first_unbought; // show first unbought level
            *reinterpret_cast<int *>(static_cast<char *>(self) + kShopItemStockOff) = remaining;      // remaining-level count
        }
    }
    if (g_orig_shop_refresh)
        g_orig_shop_refresh(self);
}

// ---- chain-open / chest-unlock per-frame hooks. Hook ::UpdateState (self == the StateMachine
// sub-object; recover base = self - 0x170): the game's MSVC linker ICF-folds the per-class ::Update
// wrappers, leaving UpdateState as the only unique per-class entry. ----
constexpr std::ptrdiff_t kStateImplSubObjOff = 0x170; // StateMachine sub-object offset (Linux _ZThn368_ thunk delta)

pal::EntityFrameFn g_chain_cb = nullptr;
pal::HookId g_chain_hook = pal::kInvalidHookId;
std::uint64_t (*g_orig_chain_update_state)(void *) = nullptr;
std::uint64_t repl_chain_update_state(void *self)
{
    const std::uint64_t ret = g_orig_chain_update_state ? g_orig_chain_update_state(self) : 0;
    if (g_chain_cb != nullptr && self != nullptr)
        g_chain_cb(static_cast<char *>(self) - kStateImplSubObjOff);
    return ret;
}

pal::EntityFrameFn g_chest_cb = nullptr;
pal::HookId g_chest_hook = pal::kInvalidHookId;
void (*g_orig_chest_update_state)(void *) = nullptr;
void repl_chest_update_state(void *self)
{
    if (g_orig_chest_update_state)
        g_orig_chest_update_state(self);
    if (g_chest_cb != nullptr && self != nullptr)
        g_chest_cb(static_cast<char *>(self) - kStateImplSubObjOff);
}

// ---- new-file starting-kit suppression. MSVC's SaveSlot layout matches Linux on depot_1875582, so the
// upgrade-field offsets are identical (verified against Windows SetItemCollected's case map). ----
constexpr std::ptrdiff_t kSparkUpgOff = 0x54;    // Spark_Upgrade   (itemType 0x46)
constexpr std::ptrdiff_t kHealthUpgOff = 0x130;  // Health_Upgrade  (itemType 0x45) bitfield (0xff = 8)
constexpr std::ptrdiff_t kMagicUpgOff = 0x170;   // Magic_Upgrade   (itemType 0x44)
constexpr std::ptrdiff_t kVialUpgOff = 0x18c;    // Vial_Upgrade    (itemType 0x47)
constexpr std::ptrdiff_t kTrinketUpgOff = 0x950; // Trinket_Upgrade (itemType 0x48)

pal::NewfileKitSuppressFn g_kit_suppress = nullptr;
pal::HookId g_kit_hook = pal::kInvalidHookId;
void (*g_orig_save_slot_clear)(void *, bool) = nullptr;
void repl_save_slot_clear(void *self, bool arg)
{
    if (g_orig_save_slot_clear)
        g_orig_save_slot_clear(self, arg);
    if (self == nullptr || !g_kit_suppress || !g_kit_suppress())
        return;
    auto field = [self](std::ptrdiff_t off) -> std::uint32_t & { return *reinterpret_cast<std::uint32_t *>(static_cast<char *>(self) + off); };
    pal::logf(pal::LogLevel::Info, "newfile-kit: zeroing default upgrades (health=%#x magic=%#x spark=%#x vial=%#x trinket=%#x)", field(kHealthUpgOff),
              field(kMagicUpgOff), field(kSparkUpgOff), field(kVialUpgOff), field(kTrinketUpgOff));
    field(kHealthUpgOff) = 0;
    field(kMagicUpgOff) = 0;
    field(kSparkUpgOff) = 0;
    field(kVialUpgOff) = 0;
    field(kTrinketUpgOff) = 0;
}

// ---- modifier control (Windows). Cheat mask = SaveSlot+0xcb0; live slot = *(g_saveManager); slot
// index = *(g_saveManager+0x8); CheatManager captured via the ActivateSaveCheats hook. Lockdown hooks
// ToggleCheat (menu: sets the runtime mirror + persists) AND SetCheatApplied (typed cheat-code).
constexpr std::ptrdiff_t kCheatMaskOff = 0xcb0;
constexpr std::ptrdiff_t kSlotIndexOff = 0x8;

std::uintptr_t g_mod_save_manager = 0;
void *g_cheat_mgr = nullptr;
bool g_mod_resolved = false;
bool g_mod_ok = false;
pal::SeedFn g_seed_fn;
pal::BlockFn g_block_fn;
std::uintptr_t g_addr_activate_slot = 0, g_addr_activate_cheats = 0, g_addr_toggle = 0, g_addr_set_applied = 0;
pal::HookId g_id_activate_slot = pal::kInvalidHookId;
pal::HookId g_id_activate_cheats = pal::kInvalidHookId;
pal::HookId g_id_toggle = pal::kInvalidHookId;
pal::HookId g_id_set_applied = pal::kInvalidHookId;
void (*g_orig_activate_slot)(void *, bool) = nullptr;
void (*g_orig_activate_cheats)(void *) = nullptr;
void (*g_orig_toggle)(void *, int, bool, void *, bool, int) = nullptr;
void (*g_orig_set_applied)(void *, int, bool, void *) = nullptr;

bool slot_looks_valid(void *p)
{
    const auto v = reinterpret_cast<std::uintptr_t>(p);
    return v >= 0x10000 && v < 0x0000800000000000;
}
void *mod_live_slot()
{
    return g_mod_save_manager != 0 ? *reinterpret_cast<void **>(g_mod_save_manager) : nullptr;
}
int mod_slot_index()
{
    return g_mod_save_manager != 0 ? *reinterpret_cast<int *>(g_mod_save_manager + kSlotIndexOff) : -1;
}
void set_mask_bit(void *slot, int idx, bool on)
{
    if (!slot_looks_valid(slot))
        return;
    auto *mask = reinterpret_cast<std::uint32_t *>(static_cast<char *>(slot) + kCheatMaskOff);
    const std::uint32_t bit = 1u << (static_cast<unsigned>(idx) & 31u);
    if (on)
        mask[idx >> 5] |= bit;
    else
        mask[idx >> 5] &= ~bit;
}

void repl_activate_slot(void *self, bool flag)
{
    void *slot = mod_live_slot();
    const int slot_index = mod_slot_index();
    pal::logf(pal::LogLevel::Debug, "modifiers: ActivateSaveSlot flag=%d slot_index=%d live=%p", static_cast<int>(flag), slot_index, slot);
    if (g_seed_fn && flag && slot_looks_valid(slot))
    {
        auto *mask = reinterpret_cast<std::uint32_t *>(static_cast<char *>(slot) + kCheatMaskOff);
        std::uint32_t words[8];
        for (int i = 0; i < 8; ++i)
            words[i] = mask[i];
        g_seed_fn(slot_index, words);
        for (int i = 0; i < 8; ++i)
            mask[i] = words[i];
        pal::logf(pal::LogLevel::Debug, "modifiers: seeded cheat mask on slot=%p (slot_index=%d)", slot, slot_index);
    }
    if (g_orig_activate_slot)
        g_orig_activate_slot(self, flag);
}
void repl_activate_cheats(void *self)
{
    g_cheat_mgr = self; // capture the CheatManager singleton (not a player path)
    if (g_orig_activate_cheats)
        g_orig_activate_cheats(self);
}
// ToggleCheat (menu toggle) writes the runtime mirror + persists; block it to keep a gameplay modifier
// off. The checkbox flips transiently but re-syncs on cursor-over. idx = 2nd arg (edx).
void repl_toggle(void *self, int idx, bool enable, void *slot, bool b, int i)
{
    if (g_cheat_mgr == nullptr)
        g_cheat_mgr = self;
    const bool blocked = g_block_fn && g_block_fn(idx);
    pal::logf(pal::LogLevel::Debug, "modifiers: ToggleCheat idx=%d enable=%d -> %s", idx, static_cast<int>(enable), blocked ? "BLOCKED" : "allowed");
    if (blocked)
        return;
    if (g_orig_toggle)
        g_orig_toggle(self, idx, enable, slot, b, i);
}
// SetCheatApplied: the persist writer the typed cheat-code path reaches directly (the menu goes
// through ToggleCheat above). idx = 2nd arg.
void repl_set_applied(void *self, int idx, bool applied, void *slot)
{
    if (g_cheat_mgr == nullptr)
        g_cheat_mgr = self;
    const bool blocked = g_block_fn && g_block_fn(idx);
    pal::logf(pal::LogLevel::Debug, "modifiers: SetCheatApplied idx=%d applied=%d -> %s", idx, static_cast<int>(applied), blocked ? "BLOCKED" : "allowed");
    if (blocked)
        return;
    if (g_orig_set_applied)
        g_orig_set_applied(self, idx, applied, slot);
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

bool current_room_index(void *room_manager, std::uint32_t *out)
{
    if (room_manager == nullptr)
        return false;
    // Room index field; +0x1b4 on the shipping build, same as Linux (the depot_1875582 RE's +0x1bc was
    // stale; MSVC and Linux lay this struct out the same here). Re-verify on a game update.
    const std::int32_t idx = *reinterpret_cast<const std::int32_t *>(static_cast<const char *>(room_manager) + 0x1b4);
    if (idx < 0)
        return false;
    *out = static_cast<std::uint32_t>(idx);
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

bool install_shop_stock_hook(ShopLevelFn level_state)
{
    g_shop_stock_cb = level_state;
    const std::uintptr_t addr = resolve_game_symbol(mth::sym::shop_item_refresh);
    if (addr == 0)
    {
        logf(LogLevel::Warn, "shop: ShopItem::Refresh not resolved; sold-out persistence disabled");
        g_shop_stock_cb = nullptr;
        return false;
    }
    g_shop_stock_hook = hook_engine().install_hook(reinterpret_cast<void *>(addr), reinterpret_cast<void *>(&repl_shop_refresh),
                                                   reinterpret_cast<void **>(&g_orig_shop_refresh));
    if (g_shop_stock_hook == kInvalidHookId)
    {
        logf(LogLevel::Error, "shop: failed to hook ShopItem::Refresh");
        g_shop_stock_cb = nullptr;
        return false;
    }
    logf(LogLevel::Info, "shop: hooked ShopItem::Refresh (id=%llu)", static_cast<unsigned long long>(g_shop_stock_hook));
    return true;
}

void remove_shop_stock_hook()
{
    if (g_shop_stock_hook != kInvalidHookId)
        hook_engine().remove_hook(g_shop_stock_hook);
    g_shop_stock_hook = kInvalidHookId;
    g_shop_stock_cb = nullptr;
    g_orig_shop_refresh = nullptr;
}

bool install_chain_open_hook(EntityFrameFn on_frame)
{
    g_chain_cb = on_frame;
    const std::uintptr_t addr = resolve_game_symbol(mth::sym::key_block_chain_update_state);
    if (addr == 0)
    {
        logf(LogLevel::Warn, "locks: KeyBlockChain::UpdateState not resolved; chain open disabled");
        return false;
    }
    g_chain_hook = hook_engine().install_hook(reinterpret_cast<void *>(addr), reinterpret_cast<void *>(&repl_chain_update_state),
                                              reinterpret_cast<void **>(&g_orig_chain_update_state));
    if (g_chain_hook == kInvalidHookId)
    {
        logf(LogLevel::Error, "locks: failed to hook KeyBlockChain::UpdateState");
        return false;
    }
    logf(LogLevel::Info, "locks: hooked KeyBlockChain::UpdateState (id=%llu)", static_cast<unsigned long long>(g_chain_hook));
    return true;
}

void remove_chain_open_hook()
{
    if (g_chain_hook != kInvalidHookId)
        hook_engine().remove_hook(g_chain_hook);
    g_chain_hook = kInvalidHookId;
    g_chain_cb = nullptr;
}

bool install_chest_unlock_hook(EntityFrameFn on_frame)
{
    g_chest_cb = on_frame;
    const std::uintptr_t addr = resolve_game_symbol(mth::sym::chest_update_state);
    if (addr == 0)
    {
        logf(LogLevel::Warn, "chest: Chest::UpdateState not resolved; chest unlock disabled");
        return false;
    }
    g_chest_hook = hook_engine().install_hook(reinterpret_cast<void *>(addr), reinterpret_cast<void *>(&repl_chest_update_state),
                                              reinterpret_cast<void **>(&g_orig_chest_update_state));
    if (g_chest_hook == kInvalidHookId)
    {
        logf(LogLevel::Error, "chest: failed to hook Chest::UpdateState");
        return false;
    }
    logf(LogLevel::Info, "chest: hooked Chest::UpdateState (id=%llu)", static_cast<unsigned long long>(g_chest_hook));
    return true;
}

void remove_chest_unlock_hook()
{
    if (g_chest_hook != kInvalidHookId)
        hook_engine().remove_hook(g_chest_hook);
    g_chest_hook = kInvalidHookId;
    g_chest_cb = nullptr;
}

bool install_newfile_kit_suppressor(NewfileKitSuppressFn should_suppress)
{
    const std::uintptr_t addr = resolve_game_symbol(mth::sym::save_slot_clear);
    if (addr == 0)
    {
        logf(LogLevel::Warn, "newfile-kit: SaveSlot::Clear not resolved; starting-kit suppression disabled");
        return false;
    }
    g_kit_suppress = std::move(should_suppress);
    g_kit_hook = hook_engine().install_hook(reinterpret_cast<void *>(addr), reinterpret_cast<void *>(&repl_save_slot_clear),
                                            reinterpret_cast<void **>(&g_orig_save_slot_clear));
    if (g_kit_hook == kInvalidHookId)
    {
        logf(LogLevel::Error, "newfile-kit: failed to hook SaveSlot::Clear");
        g_kit_suppress = nullptr;
        return false;
    }
    logf(LogLevel::Info, "newfile-kit: hooked SaveSlot::Clear (id=%llu)", static_cast<unsigned long long>(g_kit_hook));
    return true;
}

void remove_newfile_kit_suppressor()
{
    if (g_kit_hook != kInvalidHookId)
        hook_engine().remove_hook(g_kit_hook);
    g_kit_hook = kInvalidHookId;
    g_kit_suppress = nullptr;
}

bool modifiers_available()
{
    if (g_mod_resolved)
        return g_mod_ok;
    g_mod_resolved = true;
    g_mod_save_manager = resolve_game_symbol(mth::sym::save_manager);
    g_addr_activate_slot = resolve_game_symbol(mth::sym::activate_save_slot);
    g_addr_activate_cheats = resolve_game_symbol(mth::sym::activate_save_cheats);
    g_addr_toggle = resolve_game_symbol(mth::sym::toggle_cheat);
    g_addr_set_applied = resolve_game_symbol(mth::sym::set_cheat_applied);
    g_mod_ok = g_mod_save_manager != 0 && g_addr_activate_slot != 0 && g_addr_activate_cheats != 0 && g_addr_toggle != 0 && g_addr_set_applied != 0;
    if (!g_mod_ok)
        logf(LogLevel::Warn, "modifiers: Windows symbols unresolved (mgr=0x%llx slot=0x%llx cheats=0x%llx set=0x%llx); feature disabled",
             static_cast<unsigned long long>(g_mod_save_manager), static_cast<unsigned long long>(g_addr_activate_slot),
             static_cast<unsigned long long>(g_addr_activate_cheats), static_cast<unsigned long long>(g_addr_set_applied));
    return g_mod_ok;
}
void set_new_game_modifier_seed(SeedFn seed)
{
    if (!modifiers_available())
        return;
    g_seed_fn = std::move(seed);
    g_id_activate_cheats = hook_engine().install_hook(reinterpret_cast<void *>(g_addr_activate_cheats), reinterpret_cast<void *>(&repl_activate_cheats),
                                                      reinterpret_cast<void **>(&g_orig_activate_cheats));
    g_id_activate_slot = hook_engine().install_hook(reinterpret_cast<void *>(g_addr_activate_slot), reinterpret_cast<void *>(&repl_activate_slot),
                                                    reinterpret_cast<void **>(&g_orig_activate_slot));
    if (g_id_activate_cheats == kInvalidHookId)
        logf(LogLevel::Error, "modifiers: ActivateSaveCheats capture hook FAILED (live-set mirror rebuilds will be skipped)");
    if (g_id_activate_slot == kInvalidHookId)
        logf(LogLevel::Error, "modifiers: ActivateSaveSlot seed hook FAILED (new-game seeding disabled)");
    else
        logf(LogLevel::Info, "modifiers: seed hooks installed (Windows)");
}
void set_modifier_lockdown(BlockFn block)
{
    if (!modifiers_available())
        return;
    g_block_fn = std::move(block);
    // ToggleCheat = the options-menu path (mirror + persist); SetCheatApplied = the typed cheat-code path.
    g_id_toggle =
        hook_engine().install_hook(reinterpret_cast<void *>(g_addr_toggle), reinterpret_cast<void *>(&repl_toggle), reinterpret_cast<void **>(&g_orig_toggle));
    g_id_set_applied = hook_engine().install_hook(reinterpret_cast<void *>(g_addr_set_applied), reinterpret_cast<void *>(&repl_set_applied),
                                                  reinterpret_cast<void **>(&g_orig_set_applied));
    if (g_id_toggle == kInvalidHookId)
        logf(LogLevel::Error, "modifiers: ToggleCheat hook FAILED (menu lockdown disabled)");
    if (g_id_set_applied == kInvalidHookId)
        logf(LogLevel::Error, "modifiers: SetCheatApplied hook FAILED (cheat-code lockdown disabled)");
    if (g_id_toggle != kInvalidHookId && g_id_set_applied != kInvalidHookId)
        logf(LogLevel::Info, "modifiers: lockdown hooks installed (Windows)");
}
bool apply_live_modifier(int idx, bool on)
{
    if (!modifiers_available() || idx < 0 || idx >= 254)
        return false;
    void *slot = mod_live_slot();
    if (!slot_looks_valid(slot))
    {
        logf(LogLevel::Warn, "modifiers: live set idx=%d failed (no valid save slot)", idx);
        return false;
    }
    set_mask_bit(slot, idx, on);
    if (g_cheat_mgr != nullptr && g_orig_activate_cheats != nullptr)
        g_orig_activate_cheats(g_cheat_mgr); // rebuild the runtime mirror from the mask
    else
        logf(LogLevel::Warn, "modifiers: live set idx=%d bit written but mirror NOT rebuilt (CheatManager not captured yet)", idx);
    logf(LogLevel::Info, "modifiers: live set idx=%d on=%d slot=%p", idx, static_cast<int>(on), slot);
    return true;
}
void remove_modifier_hooks()
{
    for (HookId *id : {&g_id_activate_slot, &g_id_activate_cheats, &g_id_toggle, &g_id_set_applied})
    {
        if (*id != kInvalidHookId)
            hook_engine().remove_hook(*id);
        *id = kInvalidHookId;
    }
    g_seed_fn = nullptr;
    g_block_fn = nullptr;
}

} // namespace pal

// ---- per-stat level cap (Windows). The cap is inlined into the menu's state machine (no cap fn to
// detour like Linux) and the standalone UpdateState is dead code, so we wrap the per-frame entry
// LevelUpMenu::Update and, before it runs the inlined buy-gate, overwrite the stored level of every
// at-cap stat so the game's own cap gate trips (native "max level", no buy, no spend); restored after.
// Safe: the only stat-array writer is the purchase-commit, which a maxed stat never reaches.
namespace
{
constexpr std::ptrdiff_t kSaveStatArrOff = 0x174; // *(saveSlot + 0x174 + stat*4) = int stat level
constexpr int kLvlMaxRealStat = 2;                // stats 0..2 = attack/defense/sidearm; 3 = bone bank
constexpr int kMaxedLevel = 1000;                 // present an at-cap stat as this so the inlined cap gate trips

std::uintptr_t g_lc_save_manager = 0; // g_saveManager; *(g_lc_save_manager) = the active SaveSlot the menu reads
std::uintptr_t g_lc_addr_update = 0;
pal::HookId g_lc_id_update = pal::kInvalidHookId;
void (*g_lc_orig_update)(void *, void *) = nullptr; // LevelUpMenu::Update(this, ycUpdateQueueContext*)
pal::LevelCapFn g_lc_cap_fn;
bool g_lc_resolved = false;
bool g_lc_ok = false;

void *lc_active_slot()
{
    if (g_lc_save_manager == 0)
        return nullptr;
    void *slot = *reinterpret_cast<void **>(g_lc_save_manager);
    return slot_looks_valid(slot) ? slot : nullptr;
}

// LevelUpMenu::Update wrapper. Before the original runs the inlined buy-gate, overwrite the stored level
// of every real stat that has reached our cap so the inlined cap gate sees it as maxed; restore after.
// provide(stat, sentinel) returns the granted count while enforcing and the sentinel otherwise, so
// vanilla play never inflates (level < sentinel) and is untouched.
void repl_lvlup_update(void *self, void *ctx)
{
    void *slot = lc_active_slot();
    int saved[3] = {-1, -1, -1};
    if (slot != nullptr && g_lc_cap_fn)
    {
        for (int s = 0; s <= kLvlMaxRealStat; ++s)
        {
            int *lvl = reinterpret_cast<int *>(static_cast<char *>(slot) + kSaveStatArrOff + static_cast<std::ptrdiff_t>(s) * 4);
            if (*lvl >= g_lc_cap_fn(s, 0x7fffffff))
            {
                saved[s] = *lvl;
                *lvl = kMaxedLevel;
            }
        }
    }
    if (g_lc_orig_update)
        g_lc_orig_update(self, ctx);
    if (slot != nullptr)
        for (int s = 0; s <= kLvlMaxRealStat; ++s)
            if (saved[s] >= 0)
                *reinterpret_cast<int *>(static_cast<char *>(slot) + kSaveStatArrOff + static_cast<std::ptrdiff_t>(s) * 4) = saved[s];
}
} // namespace

namespace pal
{

bool level_cap_available()
{
    if (g_lc_resolved)
        return g_lc_ok;
    g_lc_resolved = true;
    g_lc_save_manager = resolve_game_symbol(mth::sym::save_manager);        // g_saveManager (whole-.text cmov scan)
    g_lc_addr_update = resolve_game_symbol(mth::sym::level_up_menu_update); // LevelUpMenu::Update (per-frame entry)
    g_lc_ok = g_lc_save_manager != 0 && g_lc_addr_update != 0;
    if (g_lc_ok)
        logf(LogLevel::Info, "levelcap: Windows resolved save-base=0x%llx update=0x%llx", static_cast<unsigned long long>(g_lc_save_manager),
             static_cast<unsigned long long>(g_lc_addr_update));
    else
        logf(LogLevel::Warn, "levelcap: Windows symbols unresolved (save=0x%llx update=0x%llx); feature disabled",
             static_cast<unsigned long long>(g_lc_save_manager), static_cast<unsigned long long>(g_lc_addr_update));
    return g_lc_ok;
}

void set_level_cap_provider(LevelCapFn cap)
{
    if (!level_cap_available())
        return;
    g_lc_cap_fn = std::move(cap);
    g_lc_id_update = hook_engine().install_hook(reinterpret_cast<void *>(g_lc_addr_update), reinterpret_cast<void *>(&repl_lvlup_update),
                                                reinterpret_cast<void **>(&g_lc_orig_update));
    if (g_lc_id_update == kInvalidHookId)
    {
        logf(LogLevel::Error, "levelcap: Windows hook install FAILED (LevelUpMenu::Update)");
        remove_level_cap_hook();
        return;
    }
    logf(LogLevel::Info, "levelcap: Windows hook installed (LevelUpMenu::Update=0x%llx)", static_cast<unsigned long long>(g_lc_addr_update));
}

void remove_level_cap_hook()
{
    if (g_lc_id_update != kInvalidHookId)
        hook_engine().remove_hook(g_lc_id_update);
    g_lc_id_update = kInvalidHookId;
    g_lc_cap_fn = nullptr;
}

} // namespace pal

// ---- capacity upgrades ----
// Per-upgrade SaveSlot field (index Magic,Health,Spark,Vial,Trinket); popcount = capacity. SaveSlot
// offsets match Linux (same struct layout). Player::UpdateStats resolves via the signature table; if
// the sig is absent (not yet regenerated for the shipping build) the feature stays disabled.
namespace
{
constexpr std::ptrdiff_t kUpgradeFieldOff[5] = {0x170, 0x130, 0x54, 0x18c, 0x950};

// Live resource-pool fields (offsets confirmed identical to Linux by 3 independent RE passes;
// CombatCore = *(Player+0x130)). Used to keep the missing amount constant on a capacity grant.
constexpr std::ptrdiff_t kCombatCoreOff = 0x130;     // Player -> CombatCore*
constexpr std::ptrdiff_t kHpCurOff = 0x1e0;          // CombatCore, float
constexpr std::ptrdiff_t kHpMaxOff = 0x1e8;          // CombatCore, float
constexpr std::ptrdiff_t kMagicCurOff = 0x1174;      // Player, int (sidearm ammo)
constexpr std::ptrdiff_t kMagicMaxOff = 0x1178;      // Player, int
constexpr std::ptrdiff_t kSparkCurOff = 0x50;        // SaveSlot, int
constexpr std::ptrdiff_t kSparkMaxOff = 0x230;       // CombatCore, int
constexpr std::ptrdiff_t kVialOverflowOff = 0x1184;  // Player, int
constexpr std::ptrdiff_t kVialOwnedBitsOff = 0x1188; // Player, int (popcount = vial capacity)
constexpr std::ptrdiff_t kVialHeldOff = 0x118c;      // Player, int
constexpr std::ptrdiff_t kVialTrinketOff = 0x1190;   // Player, int

bool g_up_resolved = false;
bool g_up_ok = false;
std::uintptr_t g_up_save_manager = 0;
void (*g_up_update_stats)(void *) = nullptr;        // Player::UpdateStats(this)
void (*g_up_set_vial_count)(void *, int) = nullptr; // Player::SetVialItemCount(total)

float fld_f(void *base, std::ptrdiff_t off)
{
    return *reinterpret_cast<float *>(static_cast<char *>(base) + off);
}
int fld_i(void *base, std::ptrdiff_t off)
{
    return *reinterpret_cast<int *>(static_cast<char *>(base) + off);
}
int vial_total(void *player)
{
    return fld_i(player, kVialOverflowOff) + std::min(fld_i(player, kVialHeldOff), fld_i(player, kVialTrinketOff));
}
int vial_capacity(void *player)
{
    return std::popcount(static_cast<unsigned>(fld_i(player, kVialOwnedBitsOff)));
}
} // namespace

namespace pal
{

bool upgrades_available()
{
    if (g_up_resolved)
        return g_up_ok;
    g_up_resolved = true;
    g_up_save_manager = resolve_game_symbol(mth::sym::save_manager);
    g_up_update_stats = reinterpret_cast<void (*)(void *)>(resolve_game_symbol(mth::sym::player_update_stats));
    g_up_set_vial_count = reinterpret_cast<void (*)(void *, int)>(resolve_game_symbol(mth::sym::player_set_vial_item_count));
    g_up_ok = g_up_save_manager != 0 && g_up_update_stats != nullptr;
    if (!g_up_ok)
        logf(LogLevel::Warn, "upgrades: symbols unresolved (save=0x%llx updatestats=0x%llx); feature disabled",
             static_cast<unsigned long long>(g_up_save_manager), reinterpret_cast<unsigned long long>(g_up_update_stats));
    return g_up_ok;
}

bool apply_upgrades(const int *counts, void *player)
{
    if (!upgrades_available() || player == nullptr)
        return false;
    void *slot = *reinterpret_cast<void **>(g_up_save_manager); // global holds the active SaveSlot*
    if (!slot_looks_valid(slot))
        return false;
    void *cc = *reinterpret_cast<void **>(static_cast<char *>(player) + kCombatCoreOff);

    // Keep each pool's missing amount constant across the grant (current = new_max - old_missing);
    // a no-op on a resend (max unchanged) and full on a fresh file. Mirrors the Linux impl.
    const float hp_missing = cc != nullptr ? fld_f(cc, kHpMaxOff) - fld_f(cc, kHpCurOff) : 0.0f;
    const int magic_missing = fld_i(player, kMagicMaxOff) - fld_i(player, kMagicCurOff);
    const int spark_missing = cc != nullptr ? fld_i(cc, kSparkMaxOff) - fld_i(slot, kSparkCurOff) : 0;
    const int vial_total_old = vial_total(player);
    const int vial_cap_old = vial_capacity(player);

    for (int i = 0; i < 5; ++i)
    {
        if (counts[i] <= 0)
            continue;
        const std::uint32_t mask = counts[i] >= 32 ? 0xFFFFFFFFu : (1u << counts[i]) - 1u; // low N bits; popcount = count
        *reinterpret_cast<std::uint32_t *>(static_cast<char *>(slot) + kUpgradeFieldOff[i]) |= mask;
    }
    g_up_update_stats(player); // recompute live maxima from the owned-bit fields

    if (cc != nullptr)
    {
        const float hp_max = fld_f(cc, kHpMaxOff);
        *reinterpret_cast<float *>(static_cast<char *>(cc) + kHpCurOff) = std::clamp(hp_max - hp_missing, 0.0f, hp_max);
        const int spark_max = fld_i(cc, kSparkMaxOff);
        *reinterpret_cast<int *>(static_cast<char *>(slot) + kSparkCurOff) = std::clamp(spark_max - spark_missing, 0, spark_max);
    }
    const int magic_max = fld_i(player, kMagicMaxOff);
    *reinterpret_cast<int *>(static_cast<char *>(player) + kMagicCurOff) = std::clamp(magic_max - magic_missing, 0, magic_max);

    const int vial_cap_new = vial_capacity(player);
    if (g_up_set_vial_count != nullptr && vial_cap_new != vial_cap_old)
        g_up_set_vial_count(player, std::max(0, vial_total_old + (vial_cap_new - vial_cap_old)));

    return true;
}

} // namespace pal

// ---- ability gating (Windows) ----
// mth::Ability ordinals (kept local so pal/ stays free of mth/ layout headers).
namespace
{
constexpr int kAbBurrow = 0;
constexpr int kAbSwim = 1;
constexpr int kAbRopeClimb = 2;
constexpr int kAbBouncePuff = 3;
constexpr int kAbBounceSpring = 4;
constexpr int kAbCarry = 5;
constexpr int kAbTrain = 6;

// Player-object offsets used by the detours; mirror the Linux struct layout (same Player struct).
constexpr std::ptrdiff_t kPlayerWaterListenerOff = 0x2c0; // WaterListener* (swim-vs-land discriminator)
constexpr std::ptrdiff_t kPlayerLowRoofFlagOff = 0x12f0;  // carry-disabled "low roof" pose flag
// PhysicsContactPair -> colliding-entity component-kind chain (shared by both CollideWith detours).
constexpr std::ptrdiff_t kContactEntityOff = 0x110;     // *(contactPair) + 0x110 -> entity
constexpr std::ptrdiff_t kEntityInteractCompOff = 0xa8; // entity + 0xa8 -> InteractComponent
constexpr std::ptrdiff_t kInteractKindOff = 0x6c;       // component + 0x6c -> int kind (8 == Player)
constexpr int kInteractKindPlayer = 8;
// TrainAuthority::OnNPCEvent case 0x15 selected-ticket-code chain; 100 = Exit (vanilla cancel).
constexpr unsigned kTrainDestPickEvent = 0x15;
constexpr std::ptrdiff_t kTrainAuthOwnerOff = 0x1b0; // this + 0x1b0 -> menu owner
constexpr std::ptrdiff_t kTrainMenuObjOff = 0xc8;    // owner + 0xc8 -> selection obj
constexpr std::ptrdiff_t kTrainSelCodeOff = 0x21c;   // obj + 0x21c -> int selected ticket itemType
constexpr int kTrainExitCode = 100;
// SaveSlot train-present byte (platform data; not an mth/ layout offset).
constexpr std::ptrdiff_t kSaveTrainPresentOff = 0x1c1;

pal::AbilityBlockFn g_ability_block;
bool g_ab_resolved = false;
bool g_ab_ok = false;

std::uintptr_t g_addr_burrow_ground = 0;
std::uintptr_t g_addr_rope_climb = 0;
std::uintptr_t g_addr_bounce_plant = 0;
std::uintptr_t g_addr_spring = 0;
std::uintptr_t g_addr_pickup = 0;
std::uintptr_t g_addr_train_npc = 0;

pal::HookId g_id_burrow = pal::kInvalidHookId;
pal::HookId g_id_rope = pal::kInvalidHookId;
pal::HookId g_id_puff = pal::kInvalidHookId;
pal::HookId g_id_spring = pal::kInvalidHookId;
pal::HookId g_id_carry = pal::kInvalidHookId;
pal::HookId g_id_train = pal::kInvalidHookId;

unsigned long (*g_orig_burrow_ground)(void *) = nullptr;
char (*g_water_is_deep)(void *, bool) = nullptr; // WaterListener::IsInDeepWaterInternal(wl, false)
void (*g_orig_rope_climb)(void *, void *, bool, bool) = nullptr;
void (*g_orig_bounce_plant)(void *, void *) = nullptr;
void (*g_orig_spring)(void *, void *) = nullptr;
unsigned long (*g_orig_pickup)(void *, bool, bool, bool) = nullptr;
void (*g_orig_train_npc)(void *, unsigned, void *) = nullptr;

bool ability_blocked(int ordinal)
{
    return g_ability_block && g_ability_block(ordinal);
}

// Free-roam burrow classify-and-commit: deep water => Swim, else Burrow. Scripted/underlab
// entrances route through SetBurrowInObject (unhooked).
unsigned long repl_burrow_ground(void *self)
{
    if (self != nullptr)
    {
        void *wl = *reinterpret_cast<void **>(static_cast<char *>(self) + kPlayerWaterListenerOff);
        const bool deep = wl != nullptr && g_water_is_deep != nullptr && g_water_is_deep(wl, false) != 0;
        const int ordinal = deep ? kAbSwim : kAbBurrow;
        if (ability_blocked(ordinal))
            return 0;
    }
    return g_orig_burrow_ground ? g_orig_burrow_ground(self) : 0;
}

// RopeClimbStart is the attach funnel; per-frame climb movement is a separate path (unhooked).
void repl_rope_climb(void *self, void *rope, bool a, bool b)
{
    if (ability_blocked(kAbRopeClimb))
        return;
    if (g_orig_rope_climb)
        g_orig_rope_climb(self, rope, a, b);
}

// Both CollideWith funnels reach the colliding entity's InteractComponent the same way.
bool collider_is_player(void *contact_pair)
{
    if (contact_pair == nullptr)
        return false;
    void *cp0 = *reinterpret_cast<void **>(contact_pair);
    if (cp0 == nullptr)
        return false;
    void *entity = *reinterpret_cast<void **>(static_cast<char *>(cp0) + kContactEntityOff);
    if (entity == nullptr)
        return false;
    void *comp = *reinterpret_cast<void **>(static_cast<char *>(entity) + kEntityInteractCompOff);
    if (comp == nullptr)
        return false;
    return *reinterpret_cast<int *>(static_cast<char *>(comp) + kInteractKindOff) == kInteractKindPlayer;
}

void repl_bounce_plant(void *self, void *contact_pair)
{
    if (collider_is_player(contact_pair) && ability_blocked(kAbBouncePuff))
        return;
    if (g_orig_bounce_plant)
        g_orig_bounce_plant(self, contact_pair);
}

void repl_spring(void *self, void *contact_pair)
{
    if (collider_is_player(contact_pair) && ability_blocked(kAbBounceSpring))
        return;
    if (g_orig_spring)
        g_orig_spring(self, contact_pair);
}

// Blocked: set the low-roof pose flag so the engine's ducked-under-roof rendering presents instead
// of popping up.
unsigned long repl_pickup(void *self, bool a, bool b, bool c)
{
    if (self != nullptr && ability_blocked(kAbCarry))
    {
        *reinterpret_cast<char *>(static_cast<char *>(self) + kPlayerLowRoofFlagOff) = 1;
        return 0;
    }
    return g_orig_pickup ? g_orig_pickup(self, a, b, c) : 0;
}

// TrainAuthority::OnNPCEvent case 0x15 picks a destination by ticket itemType. Forcing the selected
// code to Exit (100) makes vanilla treat it as a cancel.
void repl_train_npc(void *self, unsigned event, void *info)
{
    if (self != nullptr && event == kTrainDestPickEvent && ability_blocked(kAbTrain))
    {
        void *owner = *reinterpret_cast<void **>(static_cast<char *>(self) + kTrainAuthOwnerOff);
        void *obj = owner != nullptr ? *reinterpret_cast<void **>(static_cast<char *>(owner) + kTrainMenuObjOff) : nullptr;
        if (obj != nullptr)
            *reinterpret_cast<int *>(static_cast<char *>(obj) + kTrainSelCodeOff) = kTrainExitCode;
    }
    if (g_orig_train_npc)
        g_orig_train_npc(self, event, info);
}
} // namespace

namespace pal
{

bool abilities_available()
{
    if (g_ab_resolved)
        return g_ab_ok;
    g_ab_resolved = true;
    g_addr_burrow_ground = resolve_game_symbol(mth::sym::player_set_burrow_ground);
    g_water_is_deep = reinterpret_cast<char (*)(void *, bool)>(resolve_game_symbol(mth::sym::water_is_in_deep_water));
    g_addr_rope_climb = resolve_game_symbol(mth::sym::player_rope_climb_start);
    g_addr_bounce_plant = resolve_game_symbol(mth::sym::bounce_plant_collide);
    g_addr_spring = resolve_game_symbol(mth::sym::spring_bellows_collide);
    g_addr_pickup = resolve_game_symbol(mth::sym::player_pickup_carryable);
    g_addr_train_npc = resolve_game_symbol(mth::sym::train_authority_on_npc_event);
    g_ab_ok =
        g_addr_burrow_ground != 0 || g_addr_rope_climb != 0 || g_addr_bounce_plant != 0 || g_addr_spring != 0 || g_addr_pickup != 0 || g_addr_train_npc != 0;
    if (!g_ab_ok)
        logf(LogLevel::Warn, "abilities: no chokepoint symbols resolved; ability gating disabled");
    return g_ab_ok;
}

bool install_ability_hooks(AbilityBlockFn block)
{
    if (!abilities_available())
        return false;
    g_ability_block = std::move(block);

    if (g_addr_burrow_ground != 0)
    {
        g_id_burrow = hook_engine().install_hook(reinterpret_cast<void *>(g_addr_burrow_ground), reinterpret_cast<void *>(&repl_burrow_ground),
                                                 reinterpret_cast<void **>(&g_orig_burrow_ground));
        if (g_id_burrow == kInvalidHookId)
            logf(LogLevel::Error, "abilities: failed to hook Player::SetBurrowGround");
    }
    else
        logf(LogLevel::Warn, "abilities: Player::SetBurrowGround not resolved; burrow/swim gating disabled");
    if (g_water_is_deep == nullptr)
        logf(LogLevel::Warn, "abilities: IsInDeepWaterInternal not resolved; swim-vs-land treated as land");

    if (g_addr_rope_climb != 0)
    {
        g_id_rope = hook_engine().install_hook(reinterpret_cast<void *>(g_addr_rope_climb), reinterpret_cast<void *>(&repl_rope_climb),
                                               reinterpret_cast<void **>(&g_orig_rope_climb));
        if (g_id_rope == kInvalidHookId)
            logf(LogLevel::Error, "abilities: failed to hook Player::RopeClimbStart");
    }
    else
        logf(LogLevel::Warn, "abilities: Player::RopeClimbStart not resolved; rope gating disabled");

    if (g_addr_bounce_plant != 0)
    {
        g_id_puff = hook_engine().install_hook(reinterpret_cast<void *>(g_addr_bounce_plant), reinterpret_cast<void *>(&repl_bounce_plant),
                                               reinterpret_cast<void **>(&g_orig_bounce_plant));
        if (g_id_puff == kInvalidHookId)
            logf(LogLevel::Error, "abilities: failed to hook BouncePlant::CollideWith");
    }
    else
        logf(LogLevel::Warn, "abilities: BouncePlant::CollideWith not resolved; puff gating disabled");

    if (g_addr_spring != 0)
    {
        g_id_spring = hook_engine().install_hook(reinterpret_cast<void *>(g_addr_spring), reinterpret_cast<void *>(&repl_spring),
                                                 reinterpret_cast<void **>(&g_orig_spring));
        if (g_id_spring == kInvalidHookId)
            logf(LogLevel::Error, "abilities: failed to hook SpringBellows::CollideWith");
    }
    else
        logf(LogLevel::Warn, "abilities: SpringBellows::CollideWith not resolved; spring gating disabled");

    if (g_addr_pickup != 0)
    {
        g_id_carry = hook_engine().install_hook(reinterpret_cast<void *>(g_addr_pickup), reinterpret_cast<void *>(&repl_pickup),
                                                reinterpret_cast<void **>(&g_orig_pickup));
        if (g_id_carry == kInvalidHookId)
            logf(LogLevel::Error, "abilities: failed to hook Player::PickUpAnyNearbyCarryableObject");
    }
    else
        logf(LogLevel::Warn, "abilities: Player::PickUpAnyNearbyCarryableObject not resolved; carry gating disabled");

    if (g_addr_train_npc != 0)
    {
        g_id_train = hook_engine().install_hook(reinterpret_cast<void *>(g_addr_train_npc), reinterpret_cast<void *>(&repl_train_npc),
                                                reinterpret_cast<void **>(&g_orig_train_npc));
        if (g_id_train == kInvalidHookId)
            logf(LogLevel::Error, "abilities: failed to hook TrainAuthority::OnNPCEvent");
    }
    else
        logf(LogLevel::Warn, "abilities: TrainAuthority::OnNPCEvent not resolved; train gating disabled");

    const bool any = g_id_burrow != kInvalidHookId || g_id_rope != kInvalidHookId || g_id_puff != kInvalidHookId || g_id_spring != kInvalidHookId ||
                     g_id_carry != kInvalidHookId || g_id_train != kInvalidHookId;
    if (any)
        logf(LogLevel::Info, "abilities: ability gating hooks installed");
    else
        g_ability_block = nullptr;
    return any;
}

void remove_ability_hooks()
{
    for (HookId *id : {&g_id_burrow, &g_id_rope, &g_id_puff, &g_id_spring, &g_id_carry, &g_id_train})
    {
        if (*id != kInvalidHookId)
            hook_engine().remove_hook(*id);
        *id = kInvalidHookId;
    }
    g_ability_block = nullptr;
}

void enforce_train_presence(std::uintptr_t save_manager_global, bool blocked)
{
    if (!blocked)
        return; // arrival event re-shows the train
    void *slot = active_save_slot(save_manager_global);
    if (slot == nullptr)
        return;
    *reinterpret_cast<char *>(static_cast<char *>(slot) + kSaveTrainPresentOff) = 0;
}

} // namespace pal
