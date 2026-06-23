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
bool g_up_resolved = false;
bool g_up_ok = false;
std::uintptr_t g_up_save_manager = 0;
void (*g_up_update_stats)(void *) = nullptr; // Player::UpdateStats(this)
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
    for (int i = 0; i < 5; ++i)
    {
        if (counts[i] <= 0)
            continue;
        const std::uint32_t mask = counts[i] >= 32 ? 0xFFFFFFFFu : (1u << counts[i]) - 1u; // low N bits; popcount = count
        *reinterpret_cast<std::uint32_t *>(static_cast<char *>(slot) + kUpgradeFieldOff[i]) |= mask;
    }
    g_up_update_stats(player); // recompute live maxima from the owned-bit fields
    return true;
}

} // namespace pal
