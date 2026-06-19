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

// ---- chain-open / chest-unlock per-frame hooks (Linux: hook ::Update, self == entity base) ----
pal::EntityFrameFn g_chain_cb = nullptr;
pal::HookId g_chain_hook = pal::kInvalidHookId;
void (*g_orig_chain_update)(void *, void *) = nullptr;
void repl_chain_update(void *self, void *ctx)
{
    if (g_orig_chain_update)
        g_orig_chain_update(self, ctx);
    if (g_chain_cb != nullptr)
        g_chain_cb(self); // KeyBlockChain::Update gets this == base; no fixup
}

pal::EntityFrameFn g_chest_cb = nullptr;
pal::HookId g_chest_hook = pal::kInvalidHookId;
void (*g_orig_chest_update)(void *, void *) = nullptr;
void repl_chest_update(void *self, void *ctx)
{
    if (g_orig_chest_update)
        g_orig_chest_update(self, ctx);
    if (g_chest_cb != nullptr)
        g_chest_cb(self); // Chest::Update gets this == base; no fixup
}
// ---- modifier control ----
constexpr std::ptrdiff_t kCheatMaskOff = 0xcb0; // 8 u32 words: per-save enable bitmask
constexpr std::ptrdiff_t kApplySlotOff = 0x08;  // g_saveManager+0x08 = apply-path slot (garbage on build 9cd1468c)
constexpr std::ptrdiff_t kLiveSlotOff = 0x18;   // g_saveManager+0x18 = live-gameplay slot (the real one)
constexpr std::ptrdiff_t kSlotIndexOff = 0x20;  // g_saveManager+0x20 = active 0-based slot index (confirmed in-game 9cd1468c)

std::uintptr_t g_mod_save_manager = 0; // resolved g_saveManager
std::uintptr_t g_addr_activate_slot = 0;
std::uintptr_t g_addr_activate_cheats = 0;
std::uintptr_t g_addr_toggle = 0;
std::uintptr_t g_addr_set_applied = 0;
void *g_cheat_mgr = nullptr; // captured CheatManager* (this from ActivateSaveCheats/lockdown hooks)
bool g_mod_resolved = false;
bool g_mod_ok = false;

pal::SeedFn g_seed_fn;
pal::BlockFn g_block_fn;

pal::HookId g_id_activate_slot = pal::kInvalidHookId;
void (*g_orig_activate_slot)(void *, bool) = nullptr;
pal::HookId g_id_activate_cheats = pal::kInvalidHookId;
void (*g_orig_activate_cheats)(void *) = nullptr;
pal::HookId g_id_toggle = pal::kInvalidHookId;
void (*g_orig_toggle)(void *, int, bool, void *, bool, int) = nullptr;
pal::HookId g_id_set_applied = pal::kInvalidHookId;
void (*g_orig_set_applied)(void *, int, bool, void *) = nullptr;

void *mod_slot(std::ptrdiff_t off)
{
    if (g_mod_save_manager == 0)
        return nullptr;
    return *reinterpret_cast<void **>(g_mod_save_manager + off);
}

// ActivateSaveSlot also fires from title/menu init, where g_saveManager+0x08 holds an
// uninitialized (non-pointer) value; only deref a slot that looks like a canonical user pointer.
bool slot_looks_valid(void *p)
{
    const auto v = reinterpret_cast<std::uintptr_t>(p);
    return v >= 0x10000 && v < 0x0000800000000000;
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
    void *aslot = mod_slot(kApplySlotOff);
    void *lslot = mod_slot(kLiveSlotOff);
    const int slot_index = (g_mod_save_manager != 0) ? *reinterpret_cast<int *>(g_mod_save_manager + kSlotIndexOff) : -1;
    pal::logf(pal::LogLevel::Debug, "modifiers: ActivateSaveSlot flag=%d slot_index=%d apply=%p live=%p", static_cast<int>(flag), slot_index, aslot, lslot);
    // flag is true only on a real load; title/profile-menu re-activations pass false.
    if (g_seed_fn && flag)
    {
        void *slots[2] = {aslot, (lslot != aslot) ? lslot : nullptr};
        for (void *slot : slots)
        {
            if (!slot_looks_valid(slot))
                continue;
            auto *mask = reinterpret_cast<std::uint32_t *>(static_cast<char *>(slot) + kCheatMaskOff);
            std::uint32_t words[8];
            for (int i = 0; i < 8; ++i)
                words[i] = mask[i];
            g_seed_fn(slot_index, words);
            for (int i = 0; i < 8; ++i)
                mask[i] = words[i];
            pal::logf(pal::LogLevel::Debug, "modifiers: seeded cheat mask on slot=%p (slot_index=%d)", slot, slot_index);
        }
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

void repl_set_applied(void *self, int idx, bool applied, void *slot)
{
    if (g_cheat_mgr == nullptr)
        g_cheat_mgr = self;
    if (g_block_fn && g_block_fn(idx))
    {
        pal::logf(pal::LogLevel::Debug, "modifiers: blocked SetCheatApplied idx=%d (locked)", idx);
        return;
    }
    if (g_orig_set_applied)
        g_orig_set_applied(self, idx, applied, slot);
}

// ---- per-stat level cap ----
// LevelUpMenu cursor-selected stat index, read as int at this byte offset (build 16280b26 decompile
// of LevelUpMenu::UpdateState; state machine is at +0x64). Build-specific: repl_max_level logs the
// read value so it can be runtime-confirmed against the shipping build.
constexpr std::ptrdiff_t kLevelUpMenuStatOff = 0xb8;

bool g_lc_resolved = false;
bool g_lc_ok = false;
std::uintptr_t g_addr_lvlup_update = 0;
std::uintptr_t g_addr_max_level = 0;
pal::HookId g_id_lvlup_update = pal::kInvalidHookId;
pal::HookId g_id_max_level = pal::kInvalidHookId;
void (*g_orig_lvlup_update)(void *) = nullptr;
int (*g_orig_max_level)(int, int, void *) = nullptr;
pal::LevelCapFn g_cap_fn;
void *g_active_lvlup_menu = nullptr; // set only while inside UpdateState (game thread; nested call)

void repl_lvlup_update(void *self)
{
    void *prev = g_active_lvlup_menu;
    g_active_lvlup_menu = self;
    if (g_orig_lvlup_update)
        g_orig_lvlup_update(self);
    g_active_lvlup_menu = prev;
}

int repl_max_level(int a, int b, void *slot)
{
    const int vanilla = g_orig_max_level(a, b, slot);
    if (g_active_lvlup_menu == nullptr || !g_cap_fn)
        return vanilla; // called outside the level-up menu: never restrict
    const int stat = *reinterpret_cast<const int *>(static_cast<const char *>(g_active_lvlup_menu) + kLevelUpMenuStatOff);
    const int capped = g_cap_fn(stat, vanilla);
    pal::logf(pal::LogLevel::Debug, "levelcap: buy-gate stat=%d vanilla=%d -> cap=%d", stat, vanilla, capped);
    return capped;
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

bool install_chain_open_hook(EntityFrameFn on_frame)
{
    g_chain_cb = on_frame;
    const std::uintptr_t addr = resolve_game_symbol(mth::sym::key_block_chain_update);
    if (addr == 0)
    {
        logf(LogLevel::Warn, "locks: KeyBlockChain::Update not resolved; chain open disabled");
        return false;
    }
    g_chain_hook = hook_engine().install_hook(reinterpret_cast<void *>(addr), reinterpret_cast<void *>(&repl_chain_update),
                                              reinterpret_cast<void **>(&g_orig_chain_update));
    if (g_chain_hook == kInvalidHookId)
    {
        logf(LogLevel::Error, "locks: failed to hook KeyBlockChain::Update");
        return false;
    }
    logf(LogLevel::Info, "locks: hooked KeyBlockChain::Update (id=%llu)", static_cast<unsigned long long>(g_chain_hook));
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
    const std::uintptr_t addr = resolve_game_symbol(mth::sym::chest_update);
    if (addr == 0)
    {
        logf(LogLevel::Warn, "chest: Chest::Update not resolved; chest unlock disabled");
        return false;
    }
    g_chest_hook = hook_engine().install_hook(reinterpret_cast<void *>(addr), reinterpret_cast<void *>(&repl_chest_update),
                                              reinterpret_cast<void **>(&g_orig_chest_update));
    if (g_chest_hook == kInvalidHookId)
    {
        logf(LogLevel::Error, "chest: failed to hook Chest::Update");
        return false;
    }
    logf(LogLevel::Info, "chest: hooked Chest::Update (id=%llu)", static_cast<unsigned long long>(g_chest_hook));
    return true;
}

void remove_chest_unlock_hook()
{
    if (g_chest_hook != kInvalidHookId)
        hook_engine().remove_hook(g_chest_hook);
    g_chest_hook = kInvalidHookId;
    g_chest_cb = nullptr;
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
        logf(LogLevel::Warn, "modifiers: symbols unresolved (mgr=0x%llx slot=0x%llx cheats=0x%llx toggle=0x%llx set=0x%llx); feature disabled",
             static_cast<unsigned long long>(g_mod_save_manager), static_cast<unsigned long long>(g_addr_activate_slot),
             static_cast<unsigned long long>(g_addr_activate_cheats), static_cast<unsigned long long>(g_addr_toggle),
             static_cast<unsigned long long>(g_addr_set_applied));
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
        logf(LogLevel::Info, "modifiers: seed hooks installed");
}

void set_modifier_lockdown(BlockFn block)
{
    if (!modifiers_available())
        return;
    g_block_fn = std::move(block);
    g_id_toggle =
        hook_engine().install_hook(reinterpret_cast<void *>(g_addr_toggle), reinterpret_cast<void *>(&repl_toggle), reinterpret_cast<void **>(&g_orig_toggle));
    g_id_set_applied = hook_engine().install_hook(reinterpret_cast<void *>(g_addr_set_applied), reinterpret_cast<void *>(&repl_set_applied),
                                                  reinterpret_cast<void **>(&g_orig_set_applied));
    if (g_id_toggle == kInvalidHookId)
        logf(LogLevel::Error, "modifiers: ToggleCheat hook FAILED (menu lockdown disabled)");
    if (g_id_set_applied == kInvalidHookId)
        logf(LogLevel::Error, "modifiers: SetCheatApplied hook FAILED (cheat-code lockdown disabled)");
    if (g_id_toggle != kInvalidHookId && g_id_set_applied != kInvalidHookId)
        logf(LogLevel::Info, "modifiers: lockdown hooks installed");
}

bool apply_live_modifier(int idx, bool on)
{
    if (!modifiers_available() || idx < 0 || idx >= 254)
        return false;
    void *aslot = mod_slot(kApplySlotOff);
    void *lslot = mod_slot(kLiveSlotOff);
    if (!slot_looks_valid(aslot) && !slot_looks_valid(lslot))
    {
        logf(LogLevel::Warn, "modifiers: live set idx=%d failed (no valid save slot active)", idx);
        return false;
    }
    set_mask_bit(aslot, idx, on);
    if (lslot != aslot)
        set_mask_bit(lslot, idx, on);
    if (!slot_looks_valid(aslot) || !slot_looks_valid(lslot))
        logf(LogLevel::Warn, "modifiers: live set idx=%d partial (apply=%p live=%p)", idx, aslot, lslot);
    // ActivateSaveCheats reads the apply-path slot internally, so only rebuild when it is valid.
    if (slot_looks_valid(aslot) && g_cheat_mgr != nullptr && g_orig_activate_cheats != nullptr)
        g_orig_activate_cheats(g_cheat_mgr); // rebuild [CheatManager+0x20] mirror from the mask
    else
        logf(LogLevel::Warn, "modifiers: live set idx=%d bit written but mirror NOT rebuilt (apply slot/CheatManager unavailable)", idx);
    logf(LogLevel::Info, "modifiers: live set idx=%d on=%d apply=%p live=%p", idx, static_cast<int>(on), aslot, lslot);
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

bool level_cap_available()
{
    if (g_lc_resolved)
        return g_lc_ok;
    g_lc_resolved = true;
    g_addr_lvlup_update = resolve_game_symbol(mth::sym::level_up_menu_update_state);
    g_addr_max_level = resolve_game_symbol(mth::sym::get_new_game_max_level_player);
    g_lc_ok = g_addr_lvlup_update != 0 && g_addr_max_level != 0;
    if (!g_lc_ok)
        logf(LogLevel::Warn, "levelcap: symbols unresolved (update=0x%llx maxlevel=0x%llx); feature disabled",
             static_cast<unsigned long long>(g_addr_lvlup_update), static_cast<unsigned long long>(g_addr_max_level));
    return g_lc_ok;
}

void set_level_cap_provider(LevelCapFn cap)
{
    if (!level_cap_available())
        return;
    g_cap_fn = std::move(cap);
    g_id_lvlup_update = hook_engine().install_hook(reinterpret_cast<void *>(g_addr_lvlup_update), reinterpret_cast<void *>(&repl_lvlup_update),
                                                   reinterpret_cast<void **>(&g_orig_lvlup_update));
    g_id_max_level = hook_engine().install_hook(reinterpret_cast<void *>(g_addr_max_level), reinterpret_cast<void *>(&repl_max_level),
                                                reinterpret_cast<void **>(&g_orig_max_level));
    if (g_id_lvlup_update == kInvalidHookId || g_id_max_level == kInvalidHookId)
    {
        logf(LogLevel::Error, "levelcap: hook install FAILED (update id=%llu maxlevel id=%llu); rolling back",
             static_cast<unsigned long long>(g_id_lvlup_update), static_cast<unsigned long long>(g_id_max_level));
        remove_level_cap_hook(); // all-or-nothing: drop any partial hook + clear the callback
        return;
    }
    logf(LogLevel::Info, "levelcap: hooks installed (update=0x%llx maxlevel=0x%llx)", static_cast<unsigned long long>(g_addr_lvlup_update),
         static_cast<unsigned long long>(g_addr_max_level));
}

void remove_level_cap_hook()
{
    for (HookId *id : {&g_id_lvlup_update, &g_id_max_level})
    {
        if (*id != kInvalidHookId)
            hook_engine().remove_hook(*id);
        *id = kInvalidHookId;
    }
    g_cap_fn = nullptr;
    g_active_lvlup_menu = nullptr;
}

} // namespace pal
