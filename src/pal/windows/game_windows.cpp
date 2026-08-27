#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <span>
#include <string>
#include <utility>

#include "mod/mod_api.hpp"
#include "mth/core/ap/ap_ids.hpp"
#include "mth/core/data/game_layout.hpp"
#include "mth/core/data/game_symbols.hpp"
#include "mth/core/sig_scan.hpp"
#include "mth/core/stat_cap_state.hpp"
#include "mth/core/title_menu.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_hook.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_mem.hpp"
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

// Items::IsItemCollected override lives in native_mod_entry.cpp (native modding hook; cross-platform).
// This also catches clang-cl's inlined copies of IsItemCollected (e.g. the Pickup-ctor self-kill), which a
// standalone-function detour could not - so the #93 have-bit box workaround is no longer platform-specific.

// ---- modifier control (Windows). Cheat mask = SaveSlot+0xcb0; live slot = *(g_saveManager); slot
// index = *(g_saveManager+0x8); CheatManager resolved by symbol in game_cheat_manager.cpp. Lockdown hooks
// ToggleCheat (menu: sets the runtime mirror + persists) AND SetCheatApplied (typed cheat-code).
constexpr std::ptrdiff_t kCheatMaskOff = 0xcb0;
constexpr std::ptrdiff_t kSlotIndexOff = 0x8;
// The Windows save_manager symbol IS the live-slot pointer, so the master table sits at +0x10
// (Linux reaches the same table at +0x28 off a differently-based global).
constexpr std::ptrdiff_t kSaveMasterOff = 0x10;

std::uintptr_t g_mod_save_manager = 0;
bool g_mod_resolved = false;
bool g_mod_ok = false;
pal::SeedFn g_seed_fn;
pal::SaveLoadedFn g_save_loaded_fn;
pal::BlockFn g_block_fn;
std::uintptr_t g_addr_activate_slot = 0, g_addr_toggle = 0, g_addr_set_applied = 0;
pal::HookId g_id_activate_slot = pal::kInvalidHookId;
pal::HookId g_id_toggle = pal::kInvalidHookId;
pal::HookId g_id_set_applied = pal::kInvalidHookId;
void (*g_orig_activate_slot)(void *, bool) = nullptr;
void (*g_orig_toggle)(void *, int, bool, void *, bool, int) = nullptr;
void (*g_orig_set_applied)(void *, int, bool, void *) = nullptr;

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
    if (!pal::pointer_looks_valid(slot))
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
    if (g_seed_fn && flag && pal::pointer_looks_valid(slot))
    {
        auto *mask = reinterpret_cast<std::uint32_t *>(static_cast<char *>(slot) + kCheatMaskOff);
        std::uint32_t words[8];
        for (int i = 0; i < 8; ++i)
            words[i] = mask[i];
        g_seed_fn(slot_index, words);
        for (int i = 0; i < 8; ++i)
            mask[i] = words[i];
        // Diagnostic (#46): report the force-on baseline bits so we can see the seed landed. warp_home=121
        // lives in word3 bit25; landing(ossex)=128 in word4 bit0. If these read 1 here but are inert
        // in-game, the mask was seeded but never re-applied to the runtime mirror (ActivateSaveCheats).
        pal::logf(pal::LogLevel::Debug, "modifiers: seeded cheat mask on slot=%p (slot_index=%d) warp_home[121]=%d landing[128]=%d", slot, slot_index,
                  (mask[3] >> 25) & 1, mask[4] & 1);
    }
    if (g_orig_activate_slot)
        g_orig_activate_slot(self, flag);
    // Post-original: the slot's contents are the run's by now. flag=false is a title/profile-menu
    // re-activation and must not fire, or the notify lands before the save data does.
    if (flag && g_save_loaded_fn)
        g_save_loaded_fn();
}
// ToggleCheat (menu toggle) writes the runtime mirror + persists; block it to keep a gameplay modifier
// off. The checkbox flips transiently but re-syncs on cursor-over. idx = 2nd arg (edx).
void repl_toggle(void *self, int idx, bool enable, void *slot, bool b, int i)
{
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
    const bool blocked = g_block_fn && g_block_fn(idx);
    pal::logf(pal::LogLevel::Debug, "modifiers: SetCheatApplied idx=%d applied=%d -> %s", idx, static_cast<int>(applied), blocked ? "BLOCKED" : "allowed");
    if (blocked)
        return;
    if (g_orig_set_applied)
        g_orig_set_applied(self, idx, applied, slot);
}

// TitleScreen::UpdateState(): while disconnected, keep the menu cursor off index 0 ("Start Game")
// so the wrap behaves as a two-option menu. The game has no disabled-option concept to set.
pal::TitleGateFn g_title_gate_fn;
pal::HookId g_title_gate_hook = pal::kInvalidHookId;
void (*g_orig_title_update)(void *) = nullptr;

// This build's TitleScreen::UpdateState/StartGame detours are handed the SECONDARY state-machine
// base (Ghidra's param_1), not the true TitleScreen base the shared mth::layout::kTitle* constants
// are relative to. Linux's "non-virtual thunk to TitleScreen::UpdateState()" subtracts exactly this
// much before calling the true-base UpdateState, and Windows RVA 0xb0a490 reads its cursor field at
// param_1+0x128 where Linux reads true-base+0x160 (0x128 + 0x38 == 0x160), confirming the delta.
constexpr std::ptrdiff_t kTitleSecondaryBaseAdjust = 0x38;

// Desired "Start Game" label, re-applied every UpdateState so the localization refresh cannot
// leave a stale string behind. Empty means "restore the cached original" (never an English literal).
std::string g_title_start_text;
std::mutex g_title_start_text_mutex;

// First sight of the option holds the localized "Start Game"; cache it so restoring the label
// after a reconnect does not force English.
std::string g_title_start_original;
bool g_title_start_original_warned = false;
// Last label WE wrote via SetText while disconnected; lets the restore path (below) tell "undo our
// own substitution" apart from "the game changed the string for a real reason" (e.g. language change).
std::string g_title_start_last_substituted;

// Re-applies the desired Start Game text (or the cached original once connected), but only when the
// option's current text does not already match, so this cannot fight a live language change: a
// language switch while connected replaces the widget text with a new localized string that equals
// neither our cache nor our own substitution, and is therefore left alone.
void apply_title_start_text(void *base)
{
    if (base == nullptr)
        return;

    void *block = *reinterpret_cast<void **>(static_cast<char *>(base) + mth::layout::kTitleOptionBlockOff);
    if (!pal::pointer_looks_valid(block))
        return;
    void *widget = static_cast<char *>(block) + mth::layout::kTitleOptionStartGame * mth::layout::kTitleOptionStride;

    const char *cur = mod::text_of(widget);
    const bool cur_valid = pal::pointer_looks_valid(cur);
    const std::string current = cur_valid ? std::string(cur) : std::string();

    if (g_title_start_original.empty())
    {
        if (cur_valid)
            g_title_start_original = current;
        else if (!g_title_start_original_warned)
        {
            g_title_start_original_warned = true;
            pal::logf(pal::LogLevel::Warn, "title: could not read the Start Game option's original text; label will not be restored");
        }
    }

    std::string want;
    {
        std::lock_guard<std::mutex> lk(g_title_start_text_mutex);
        want = g_title_start_text;
    }

    if (!want.empty())
    {
        if (current != want && mod::set_text(widget, want.c_str()))
            g_title_start_last_substituted = want;
    }
    else if (!g_title_start_original.empty() && current != g_title_start_original && current == g_title_start_last_substituted)
    {
        mod::set_text(widget, g_title_start_original.c_str());
    }
}

// Latches on an implausible selected index, which is what a wrong secondary-base guess would read:
// a wrong base can still look plausible on a later call, so one failure disables the gate for good.
bool g_title_base_disabled = false;

void repl_title_update_state(void *self)
{
    char *base = self != nullptr ? static_cast<char *>(self) - kTitleSecondaryBaseAdjust : nullptr;
    int *idx = base != nullptr ? reinterpret_cast<int *>(base + mth::layout::kTitleSelectedIndexOff) : nullptr;
    const bool base_ok = idx != nullptr && *idx >= 0 && *idx < mth::layout::kTitleOptionCount;
    if (idx != nullptr && !base_ok && !g_title_base_disabled)
    {
        g_title_base_disabled = true;
        pal::logf(pal::LogLevel::Warn,
                  "title: derived base selected-index=%d out of [0,%d); -0x38 secondary-base guess may be "
                  "wrong, cursor gate/label permanently disabled",
                  *idx, mth::layout::kTitleOptionCount);
    }
    const int previous = base_ok ? *idx : 0;

    if (g_orig_title_update)
        g_orig_title_update(self);

    if (!base_ok || g_title_base_disabled)
        return; // guard tripped (this call or a prior one): never write cursor/text through an unverified base

    if (g_title_gate_fn && !g_title_gate_fn())
    {
        // After the game's own wrap, and direction-aware: see mth::skip_gated_option.
        *idx = mth::skip_gated_option(previous, *idx);
    }

    apply_title_start_text(base);
}

} // namespace

namespace pal
{

void *active_save_slot(std::uintptr_t save_manager_global)
{
    if (save_manager_global == 0)
        return nullptr;
    void *slot = *reinterpret_cast<void **>(save_manager_global); // the global already holds the active SaveSlot*
    // Fail closed on an uninitialized/garbage slot; every caller null-checks.
    return pal::pointer_looks_valid(slot) ? slot : nullptr;
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

bool install_title_gate_hook(TitleGateFn connected)
{
    g_title_gate_fn = std::move(connected);
    const std::uintptr_t addr = resolve_game_symbol(mth::sym::title_screen_update_state);
    if (addr == 0)
    {
        logf(LogLevel::Warn, "title: TitleScreen::UpdateState not resolved; menu gating off");
        g_title_gate_fn = nullptr;
        return false;
    }
    g_title_gate_hook = hook_engine().install_hook(reinterpret_cast<void *>(addr), reinterpret_cast<void *>(&repl_title_update_state),
                                                   reinterpret_cast<void **>(&g_orig_title_update));
    if (g_title_gate_hook == kInvalidHookId)
    {
        logf(LogLevel::Error, "title: failed to hook TitleScreen::UpdateState");
        g_title_gate_fn = nullptr;
        return false;
    }
    logf(LogLevel::Info, "title: hooked TitleScreen::UpdateState (id=%llu)", static_cast<unsigned long long>(g_title_gate_hook));
    return true;
}

void remove_title_gate_hook()
{
    if (g_title_gate_hook != kInvalidHookId)
        hook_engine().remove_hook(g_title_gate_hook);
    g_title_gate_hook = kInvalidHookId;
    g_title_gate_fn = nullptr;
}

void set_title_start_option_text(const char *text)
{
    std::lock_guard<std::mutex> lk(g_title_start_text_mutex);
    g_title_start_text = (text != nullptr) ? text : "";
}

// install_item_collected_hook / remove_item_collected_hook live in mod/mod_api.cpp.

bool modifiers_available()
{
    if (g_mod_resolved)
        return g_mod_ok;
    g_mod_resolved = true;
    g_mod_save_manager = resolve_game_symbol(mth::sym::save_manager);
    g_addr_activate_slot = resolve_game_symbol(mth::sym::activate_save_slot);
    g_addr_toggle = resolve_game_symbol(mth::sym::toggle_cheat);
    g_addr_set_applied = resolve_game_symbol(mth::sym::set_cheat_applied);
    g_mod_ok = g_mod_save_manager != 0 && g_addr_activate_slot != 0 && g_addr_toggle != 0 && g_addr_set_applied != 0;
    if (!g_mod_ok)
        logf(LogLevel::Warn, "modifiers: Windows symbols unresolved (mgr=0x%llx slot=0x%llx set=0x%llx); feature disabled",
             static_cast<unsigned long long>(g_mod_save_manager), static_cast<unsigned long long>(g_addr_activate_slot),
             static_cast<unsigned long long>(g_addr_set_applied));
    return g_mod_ok;
}
void set_save_loaded(SaveLoadedFn cb)
{
    g_save_loaded_fn = std::move(cb);
}

void set_new_game_modifier_seed(SeedFn seed)
{
    if (!modifiers_available())
        return;
    g_seed_fn = std::move(seed);
    g_id_activate_slot = hook_engine().install_hook(reinterpret_cast<void *>(g_addr_activate_slot), reinterpret_cast<void *>(&repl_activate_slot),
                                                    reinterpret_cast<void **>(&g_orig_activate_slot));
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
    if (!pal::pointer_looks_valid(slot))
    {
        logf(LogLevel::Warn, "modifiers: live set idx=%d failed (no valid save slot)", idx);
        return false;
    }
    // The native entry resolves the active slot itself, so this drops our own save-manager offset
    // guess, and it stamps the extra save-file byte the four special indices need.
    if (!mod::cheat_manager_set_cheat_applied(idx, on))
        set_mask_bit(slot, idx, on); // pre-appended-API build: fall back to the raw mask write
    if (!mod::cheat_manager_activate_save_cheats())
        logf(LogLevel::Warn, "modifiers: live set idx=%d bit written but mirror NOT rebuilt (native ActivateSaveCheats unavailable)", idx);
    logf(LogLevel::Info, "modifiers: live set idx=%d on=%d slot=%p", idx, static_cast<int>(on), slot);
    return true;
}
void remove_modifier_hooks()
{
    for (HookId *id : {&g_id_activate_slot, &g_id_toggle, &g_id_set_applied})
    {
        if (*id != kInvalidHookId)
            hook_engine().remove_hook(*id);
        *id = kInvalidHookId;
    }
    g_seed_fn = nullptr;
    g_save_loaded_fn = nullptr;
    g_block_fn = nullptr;
}

} // namespace pal

// ---- per-stat level cap (Windows). The cap is inlined into the menu's state machine (no cap fn to detour
// like Linux), so we wrap LevelUpMenu::Update and, before its inlined buy-gate runs, fake the cursor-
// selected at-cap stat's stored level to trip the game's own cap gate (native "max level", no buy, no
// spend); restored after. Only the selected stat is faked: the commit handler re-applies the defense level
// (SaveSlot+0x178) to combat and the bank/level rows commit without the buy-gate, so faking an unselected
// stat would leave combat defense pinned to the sentinel for the screen.
namespace
{
constexpr std::ptrdiff_t kSaveStatArrOff = 0x174; // *(saveSlot + 0x174 + stat*4) = int stat level
constexpr int kLvlMaxRealStat = 2;                // stats 0..2 = attack/defense/sidearm; 3 = bone bank
constexpr int kMaxedLevel = 1000;                 // present an at-cap stat as this so the inlined cap gate trips
// LevelUpMenu state field on the derived `this` (r148714): the state-machine switch value. 3 = the
// interactive selection/row-display state that runs the inlined buy-gate + SetDescription; 0 = entry gate,
// 4 = commit, 6 = reopen. We present capped stats as maxed only in state 3, so the entry gate stays on the
// real level (menu opens, bank row reachable, level-up pulse resolves) and the commit recompute is never fed
// a fake level.
constexpr std::ptrdiff_t kLevelUpMenuStateOff = 0x64;
constexpr int kLevelUpMenuInteractiveState = 3;
// menu cursor row (int): 0..2 real stats, 3/4 level/bank. Same offset as Linux; the fake is scoped to it.
constexpr std::ptrdiff_t kLevelUpMenuStatOff = 0xb8;

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
    return pal::pointer_looks_valid(slot) ? slot : nullptr;
}

// LevelUpMenu::Update wrapper: fake the cursor-selected at-cap stat's level so the inlined cap gate reads it
// as maxed, then restore. provide(stat, sentinel) returns the granted count while enforcing and the sentinel
// otherwise, so vanilla play (level < sentinel) is untouched.
void repl_lvlup_update(void *self, void *ctx)
{
    void *slot = lc_active_slot();
    int saved[3] = {-1, -1, -1};
    // Interactive state only: faking at the entry state (0) would trip the open gate (menu never opens).
    const int menu_state = self != nullptr ? *reinterpret_cast<int *>(static_cast<char *>(self) + kLevelUpMenuStateOff) : -1;
    const int cursor_stat = self != nullptr ? *reinterpret_cast<int *>(static_cast<char *>(self) + kLevelUpMenuStatOff) : -1;
    const bool interactive = menu_state == kLevelUpMenuInteractiveState;
    if (slot != nullptr && g_lc_cap_fn)
    {
        for (int s = 0; s <= kLvlMaxRealStat; ++s)
        {
            int *lvl = reinterpret_cast<int *>(static_cast<char *>(slot) + kSaveStatArrOff + static_cast<std::ptrdiff_t>(s) * 4);
            if (mth::boneup_fake_capped_stat(interactive, s == cursor_stat, *lvl, g_lc_cap_fn(s, 0x7fffffff)))
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
    pal::boneup_annotate_description(self);
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

// Pending bounce target (3 floats, FLT_MAX when none) and the bone-bounce variant marker. Player::OnBounce
// consumes both; its own early-out clears them, and the block path has to do the same or the target stays
// armed and fires late (#168).
constexpr std::ptrdiff_t kPlayerBounceTargetOff = 0x252c;
constexpr std::ptrdiff_t kPlayerBounceMarkerOff = 0x24c0;
// PhysicsContactPair -> colliding-entity component-kind chain (shared by both CollideWith detours).
constexpr std::ptrdiff_t kContactEntityOff = 0x110;     // *(contactPair) + 0x110 -> entity
constexpr std::ptrdiff_t kEntityInteractCompOff = 0xa8; // entity + 0xa8 -> InteractComponent
constexpr std::ptrdiff_t kInteractKindOff = 0x6c;       // component + 0x6c -> int kind (8 == Player)
constexpr int kInteractKindPlayer = 8;
// TrainAuthority::OnNPCEvent case 0x15 selected-ticket-code chain; 100 = Exit (vanilla cancel).
constexpr unsigned kTrainDestPickEvent = 0x15;
// OnNPCEvent case 1 (interact/dialogue) and case 9 (TriggerRideDone/warp) carry the CTP boss ride gate.
constexpr unsigned kTrainInteractEvent = 1;
constexpr unsigned kTrainRideDoneEvent = 9;
// Windows OnNPCEvent gets a `this` adjusted +0x170 vs the Linux struct base (MI thunk), so the owner is at
// +0x40 here where Linux reads +0x1b0. The game's case-0x15 chain is self+0x40 -> +0xc8 -> +0x21c.
constexpr std::ptrdiff_t kTrainAuthOwnerOff = 0x40; // this + 0x40 -> menu owner
constexpr std::ptrdiff_t kTrainMenuObjOff = 0xc8;   // owner + 0xc8 -> selection obj
constexpr std::ptrdiff_t kTrainSelCodeOff = 0x21c;  // obj + 0x21c -> int selected ticket itemType
constexpr int kTrainExitCode = 100;
// SaveSlot generic Train Pass (item 94) owned byte. Set by Items::OnPickupDone on collect; gates boarding
// (train presence) under train_rando. Platform data; not an mth/ layout offset.
constexpr std::ptrdiff_t kSaveTrainPassOwnedOff = 0x1c0;
// SaveSlot train-present byte (platform data; not an mth/ layout offset).
constexpr std::ptrdiff_t kSaveTrainPresentOff = 0x1c1;
// CTP boss (Thorne 2) defeated bit: byte +0x281 mask 0x02 of the SaveSlot+0x280 boss bitfield. The Coltrane
// line ride is gated on it (#108). Shared layout.
constexpr std::ptrdiff_t kSaveCtpBossByteOff = 0x281;
constexpr std::uint8_t kCtpBossGateMask = 0x02;

// train_rando destination gate, published from mth each tick. When active, repl_train_npc cancels any
// ticket line whose bit isn't in the granted mask (the +0x1e0 menu clamp can't hide lines 95/99); when
// inactive it falls back to the console Train-ability block.
std::uint32_t g_train_granted_mask = 0;
bool g_train_rando_gate = false;

pal::AbilityBlockFn g_ability_block;
bool g_ab_resolved = false;
bool g_ab_ok = false;

std::uintptr_t g_addr_burrow_ground = 0;
std::uintptr_t g_addr_rope_climb = 0;
std::uintptr_t g_addr_bounce_plant = 0;
std::uintptr_t g_addr_bounce_launch = 0;
std::uintptr_t g_addr_on_bounce = 0;
std::uintptr_t g_addr_spring = 0;
std::uintptr_t g_addr_pickup = 0;
std::uintptr_t g_addr_train_npc = 0;
std::uintptr_t g_addr_burrow_jump = 0; // #56

pal::HookId g_id_burrow = pal::kInvalidHookId;
pal::HookId g_id_rope = pal::kInvalidHookId;
pal::HookId g_id_puff = pal::kInvalidHookId;
pal::HookId g_id_launch = pal::kInvalidHookId;
pal::HookId g_id_on_bounce = pal::kInvalidHookId;
pal::HookId g_id_spring = pal::kInvalidHookId;
pal::HookId g_id_carry = pal::kInvalidHookId;
pal::HookId g_id_train = pal::kInvalidHookId;
pal::HookId g_id_burrow_jump = pal::kInvalidHookId; // #56

unsigned long (*g_orig_burrow_ground)(void *) = nullptr;
void (*g_orig_rope_climb)(void *, void *, bool, bool) = nullptr;
void (*g_orig_bounce_plant)(void *, void *) = nullptr;
void (*g_orig_bounce_launch)(void *, void *) = nullptr;
void (*g_orig_on_bounce)(void *) = nullptr;
void (*g_orig_spring)(void *, void *) = nullptr;
unsigned long (*g_orig_pickup)(void *, bool, bool, bool) = nullptr;
void (*g_orig_train_npc)(void *, unsigned, void *) = nullptr;
void (*g_orig_burrow_jump)(void *) = nullptr; // #56

pal::BurrowCommitFn g_burrow_commit; // #163: burrow lifetime observers (arm/disarm the boundary watcher)
pal::BurrowEmergeFn g_burrow_emerge;

bool ability_blocked(int ordinal)
{
    return g_ability_block && g_ability_block(ordinal);
}

// Reads the swim-vs-land discriminator off a player. Shared by the commit classifier and the per-tick
// boundary poll so both see exactly the same signal.
bool player_in_deep_water(void *player)
{
    if (player == nullptr)
        return false;
    void *wl = *reinterpret_cast<void **>(static_cast<char *>(player) + kPlayerWaterListenerOff);
    return mod::water_is_in_deep_water(wl, false);
}

// Free-roam burrow classify-and-commit: deep water => Swim, else Burrow. Scripted/underlab
// entrances route through SetBurrowInObject (unhooked).
unsigned long repl_burrow_ground(void *self)
{
    bool deep = false;
    if (self != nullptr)
    {
        deep = player_in_deep_water(self);
        const int ordinal = deep ? kAbSwim : kAbBurrow;
        if (ability_blocked(ordinal))
            return 0;
    }
    const unsigned long r = g_orig_burrow_ground ? g_orig_burrow_ground(self) : 0;
    // A commit we let through: the boundary watcher now knows which mode the player is in, so it can catch
    // the game flipping between them mid-burrow without reading the mode field (#163).
    if (self != nullptr && g_burrow_commit)
        g_burrow_commit(deep);
    return r;
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

// Out-of-line launch for ground/burrow-underable puffs (player is the second arg, always the launchee);
// the floating case bounces inline in CollideWith above (issue #47).
void repl_bounce_launch(void *self, void *player)
{
    if (ability_blocked(kAbBouncePuff))
        return;
    if (g_orig_bounce_launch)
        g_orig_bounce_launch(self, player);
}

// Player::OnBounce is the only consumer of the pending target and the only writer of the launch velocity,
// so it catches every bounce surface, including the Bone Beach bushes, which are Breakables reached by a
// dispatch inlined into Player::Update / SlideOutOfWall that neither detour above sees (#168). The listener
// sweep calls it unconditionally every tick, so the blocked path must stay allocation- and log-free.
void repl_on_bounce(void *player)
{
    if (player != nullptr && ability_blocked(kAbBouncePuff))
    {
        float *target = reinterpret_cast<float *>(static_cast<char *>(player) + kPlayerBounceTargetOff);
        target[0] = target[1] = target[2] = std::numeric_limits<float>::max();
        *(static_cast<char *>(player) + kPlayerBounceMarkerOff) = 0;
        return;
    }
    if (g_orig_on_bounce)
        g_orig_on_bounce(player);
}

void repl_spring(void *self, void *contact_pair)
{
    if (collider_is_player(contact_pair) && ability_blocked(kAbBounceSpring))
        return;
    if (g_orig_spring)
        g_orig_spring(self, contact_pair);
}

// Blocked: just refuse the grab. This used to also set Player+0x12f0, mislabelled a "low roof pose" flag:
// it actually arms the burrow-landing shockwave and delays the hop-out. The game probes for carryables on
// ledge jumps and landings too, so every probe armed a spurious shockwave. Holding Mina under an overhead
// carryable is repl_burrow_jump's emerge-suppress, not a flag poke.
unsigned long repl_pickup(void *self, bool a, bool b, bool c)
{
    if (self != nullptr && ability_blocked(kAbCarry))
        return 0;
    return g_orig_pickup ? g_orig_pickup(self, a, b, c) : 0;
}

// #56 Windows port of the carry emerge-suppress (see game_linux.cpp for the rationale). Read-only replica
// of the game's grab query; deref chains/offsets verified identical to Linux on MSVC build 6a406cb9.
bool carryable_overhead(void *self)
{
    if (self == nullptr)
        return false;
    char *P = static_cast<char *>(self);
    void *e0 = *reinterpret_cast<void **>(P + 0x278);
    void *E = e0 != nullptr ? *reinterpret_cast<void **>(static_cast<char *>(e0) + 0x70) : nullptr;
    if (E == nullptr)
        return false;
    char *Ec = static_cast<char *>(E);
    void *comp = *reinterpret_cast<void **>(Ec + 0x10);
    void *phys = *reinterpret_cast<void **>(Ec + 0xb0);
    const int layer = *reinterpret_cast<int *>(Ec + 0x15c);
    void *w0 = *reinterpret_cast<void **>(P + 0x10);
    void *w1 = w0 != nullptr ? *reinterpret_cast<void **>(static_cast<char *>(w0) + 0x50) : nullptr;
    void *mgr = w1 != nullptr ? *reinterpret_cast<void **>(static_cast<char *>(w1) + 0x1748) : nullptr;
    if (phys == nullptr || comp == nullptr || mgr == nullptr)
        return false;

    float box[6]{};
    if (!mod::physics_get_aabb(phys, box, false, 0u))
        return false;
    const float fd8 = *reinterpret_cast<float *>(static_cast<char *>(comp) + 0xd8);
    const float clamp = (fd8 < 0.0f ? -fd8 : fd8) * 1.6f * 0.3f;
    if (clamp < box[5])
        box[5] = clamp;

    int out_n = 0;
    return mod::closest_carryable(mgr, box, layer, 1.6f, &out_n, 0ull) != nullptr;
}

void repl_burrow_jump(void *self)
{
    if (self != nullptr && ability_blocked(kAbCarry) && carryable_overhead(self))
    {
        pal::logf(pal::LogLevel::Debug, "carry: burrow-emerge suppressed (carryable overhead)");
        return; // still burrowed, so the boundary watcher keeps its arm
    }
    if (g_orig_burrow_jump)
        g_orig_burrow_jump(self);
    if (g_burrow_emerge)
        g_burrow_emerge();
}

// TrainAuthority::OnNPCEvent case 0x15 picks a destination by ticket itemType. Forcing the selected
// code to Exit (100) makes vanilla treat it as a cancel.
void repl_train_npc(void *self, unsigned event, void *info)
{
    if (self != nullptr && event == kTrainDestPickEvent)
    {
        void *owner = *reinterpret_cast<void **>(static_cast<char *>(self) + kTrainAuthOwnerOff);
        void *obj = owner != nullptr ? *reinterpret_cast<void **>(static_cast<char *>(owner) + kTrainMenuObjOff) : nullptr;
        if (obj != nullptr)
        {
            int *code = reinterpret_cast<int *>(static_cast<char *>(obj) + kTrainSelCodeOff);
            // Backstop to the SetupBoxes patch: cancel a picked line that isn't AP-granted.
            const bool block = g_train_rando_gate ? mth::train_destination_blocked(*code, g_train_granted_mask) : ability_blocked(kAbTrain);
            if (block)
                *code = kTrainExitCode;
        }
    }

    // #108: the Coltrane line ride is gated on the CTP boss (Thorne 2), softlocking a player who leaves CTP
    // first. Once the pass is owned, set the boss bit only across the original call so the ride completes,
    // restoring it after so the save is never marked (the boss still arms and fights).
    void *slot = nullptr;
    std::uint8_t saved_boss = 0;
    bool bypass = false;
    if (event == kTrainInteractEvent || event == kTrainRideDoneEvent)
    {
        slot = pal::active_save_slot(g_mod_save_manager);
        if (slot != nullptr && *reinterpret_cast<std::uint8_t *>(static_cast<char *>(slot) + kSaveTrainPassOwnedOff) != 0)
        {
            auto *gate = reinterpret_cast<std::uint8_t *>(static_cast<char *>(slot) + kSaveCtpBossByteOff);
            saved_boss = *gate;
            *gate = static_cast<std::uint8_t>(saved_boss | kCtpBossGateMask);
            bypass = true;
        }
    }
    if (g_orig_train_npc)
        g_orig_train_npc(self, event, info);
    if (bypass)
        *reinterpret_cast<std::uint8_t *>(static_cast<char *>(slot) + kSaveCtpBossByteOff) = saved_boss;
}

// One-time .text patch of ShopMenu::SetupBoxes' hardcoded "always-shown" train-line test. MSVC expands the
// mask into `cmp r8d,{0x5e,0x5f,0x63}; je shown`; the 0x63 (line 99, Coltrane Peak) compare forces its box
// selectable regardless of ticket, defeating the +0x1e0 clamp. Neutralize just that compare (imm 0x63->0xff,
// an impossible itemType) so 99 falls through to the SaveSlot+0x1e0 gate (driven by enforce_train_destinations),
// leaving 0x5e/0x5f (board / Ossex-HUB) always-shown. #98.
void patch_train_destination_menu()
{
    const pal::ModuleInfo gm = pal::game_module();
    if (gm.base == 0 || gm.size == 0)
    {
        pal::logf(pal::LogLevel::Warn, "train: game module unavailable; SetupBoxes mask patch skipped (99 stays always-shown)");
        return;
    }
    // Anchor on the three cmp/je triples: cmp r8d,0x5e;je .. cmp r8d,0x5f;je .. cmp r8d,0x63;je (0x63 at idx 15).
    static const std::uint8_t pat[] = {0x41, 0x83, 0xF8, 0x5E, 0x74, 0x00, 0x41, 0x83, 0xF8, 0x5F, 0x74, 0x00, 0x41, 0x83, 0xF8, 0x63, 0x74, 0x00};
    static const std::uint8_t msk[] = {1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0};
    constexpr std::size_t kColtraneImm = 15;
    const std::span<const std::uint8_t> region{reinterpret_cast<const std::uint8_t *>(gm.base), gm.size};
    const mth::sig::Match m = mth::sig::find_masked(region, pat, msk, sizeof(pat));
    if (!m.found || !m.unique)
    {
        pal::logf(pal::LogLevel::Warn, "train: SetupBoxes mask site %s; Coltrane may stay always-shown", m.found ? "ambiguous" : "not found");
        return;
    }
    auto *site = reinterpret_cast<std::uint8_t *>(gm.base + m.offset + kColtraneImm);
    if (*site != 0x63)
    {
        pal::logf(pal::LogLevel::Warn, "train: SetupBoxes Coltrane cmp imm=0x%02x (expected 0x63); patch skipped", *site);
        return;
    }
    const std::uint8_t patched = 0xFF;
    if (!pal::patch_code(site, &patched, 1))
    {
        pal::logf(pal::LogLevel::Error, "train: SetupBoxes mask patch_code failed");
        return;
    }
    pal::logf(pal::LogLevel::Info, "train: SetupBoxes Coltrane cmp 0x63->0xff at 0x%llx (un-ticketed Coltrane now non-selectable)",
              static_cast<unsigned long long>(gm.base + m.offset + kColtraneImm));
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
    g_addr_rope_climb = resolve_game_symbol(mth::sym::player_rope_climb_start);
    g_addr_bounce_plant = resolve_game_symbol(mth::sym::bounce_plant_collide);
    g_addr_bounce_launch = resolve_game_symbol(mth::sym::bounce_plant_launch);
    g_addr_on_bounce = resolve_game_symbol(mth::sym::player_on_bounce);
    g_addr_spring = resolve_game_symbol(mth::sym::spring_bellows_collide);
    g_addr_pickup = resolve_game_symbol(mth::sym::player_pickup_carryable);
    g_addr_train_npc = resolve_game_symbol(mth::sym::train_authority_on_npc_event);
    g_addr_burrow_jump = resolve_game_symbol(mth::sym::mina_on_burrow_jump); // #56
    g_ab_ok = g_addr_burrow_ground != 0 || g_addr_rope_climb != 0 || g_addr_bounce_plant != 0 || g_addr_bounce_launch != 0 || g_addr_on_bounce != 0 ||
              g_addr_spring != 0 || g_addr_pickup != 0 || g_addr_train_npc != 0;
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
    if (!mod::water_api_available())
        logf(LogLevel::Warn, "abilities: WaterListenerIsInDeepWater unavailable; swim-vs-land treated as land");

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

    if (g_addr_bounce_launch != 0)
    {
        g_id_launch = hook_engine().install_hook(reinterpret_cast<void *>(g_addr_bounce_launch), reinterpret_cast<void *>(&repl_bounce_launch),
                                                 reinterpret_cast<void **>(&g_orig_bounce_launch));
        if (g_id_launch == kInvalidHookId)
            logf(LogLevel::Error, "abilities: failed to hook BouncePlant::BounceLaunch");
    }
    else
        logf(LogLevel::Warn, "abilities: BouncePlant::BounceLaunch not resolved; ground-puff gating disabled");

    if (g_addr_on_bounce != 0) // #168: universal launch gate (bounce bushes and any other non-plant surface)
    {
        g_id_on_bounce = hook_engine().install_hook(reinterpret_cast<void *>(g_addr_on_bounce), reinterpret_cast<void *>(&repl_on_bounce),
                                                    reinterpret_cast<void **>(&g_orig_on_bounce));
        if (g_id_on_bounce == kInvalidHookId)
            logf(LogLevel::Error, "abilities: failed to hook Player::OnBounce");
    }
    else
        logf(LogLevel::Warn, "abilities: Player::OnBounce not resolved; bounce-bush gating disabled");

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

    patch_train_destination_menu(); // make un-ticketed Coltrane Peak (line 99) non-selectable (#98)

    if (g_addr_burrow_jump != 0) // #56: suppress the burrow-emerge when a carryable is overhead
    {
        g_id_burrow_jump = hook_engine().install_hook(reinterpret_cast<void *>(g_addr_burrow_jump), reinterpret_cast<void *>(&repl_burrow_jump),
                                                      reinterpret_cast<void **>(&g_orig_burrow_jump));
        if (g_id_burrow_jump == kInvalidHookId)
            logf(LogLevel::Warn, "abilities: failed to hook Mina::OnBurrowJump (#56)");
    }
    else
        logf(LogLevel::Warn, "abilities: Mina::OnBurrowJump not resolved; carry emerge-suppress disabled");

    const bool any = g_id_burrow != kInvalidHookId || g_id_rope != kInvalidHookId || g_id_puff != kInvalidHookId || g_id_launch != kInvalidHookId ||
                     g_id_on_bounce != kInvalidHookId || g_id_spring != kInvalidHookId || g_id_carry != kInvalidHookId || g_id_train != kInvalidHookId ||
                     g_id_burrow_jump != kInvalidHookId;
    if (any)
        logf(LogLevel::Info, "abilities: ability gating hooks installed");
    else
        g_ability_block = nullptr;
    return any;
}

void remove_ability_hooks()
{
    for (HookId *id : {&g_id_burrow, &g_id_rope, &g_id_puff, &g_id_launch, &g_id_on_bounce, &g_id_spring, &g_id_carry, &g_id_train, &g_id_burrow_jump})
    {
        if (*id != kInvalidHookId)
            hook_engine().remove_hook(*id);
        *id = kInvalidHookId;
    }
    g_ability_block = nullptr;
    g_burrow_commit = nullptr;
    g_burrow_emerge = nullptr;
}

void set_burrow_observers(BurrowCommitFn on_commit, BurrowEmergeFn on_emerge)
{
    g_burrow_commit = std::move(on_commit);
    g_burrow_emerge = std::move(on_emerge);
}

int burrow_water_state(void *player)
{
    if (player == nullptr || !mod::water_api_available())
        return -1;
    void *wl = *reinterpret_cast<void **>(static_cast<char *>(player) + kPlayerWaterListenerOff);
    if (wl == nullptr)
        return -1;
    return mod::water_is_in_deep_water(wl, false) ? 1 : 0;
}

bool force_burrow_emerge(void *player)
{
    if (player == nullptr || g_orig_burrow_jump == nullptr)
        return false;
    g_orig_burrow_jump(player); // the original, not the detour: this emerge is forced, not player-initiated
    return true;
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

void enforce_train_boarding(std::uintptr_t save_manager_global)
{
    void *slot = active_save_slot(save_manager_global);
    if (slot == nullptr)
        return;
    // Require the generic Train Pass (#98): while +0x1c0 (pass owned) is 0, keep the train hidden so it
    // cannot be boarded. Once the pass is received (OnPickupDone sets +0x1c0) leave the story-set presence.
    if (*reinterpret_cast<std::uint8_t *>(static_cast<char *>(slot) + kSaveTrainPassOwnedOff) == 0)
        *reinterpret_cast<char *>(static_cast<char *>(slot) + kSaveTrainPresentOff) = 0;
}

void set_train_destination_gate(std::uint32_t granted_mask, bool rando_active)
{
    g_train_granted_mask = granted_mask;
    g_train_rando_gate = rando_active;
}

// Resolved on first use rather than eagerly: on Windows each resolve is a full .text signature scan,
// and the staging path runs on a game frame.
std::uintptr_t g_takeover_save_manager = 0;
std::uintptr_t g_takeover_slot_clear = 0;
std::uintptr_t g_takeover_init_gamestate = 0;
bool g_takeover_syms_resolved = false;

void resolve_takeover_symbols()
{
    if (g_takeover_syms_resolved)
        return;
    g_takeover_syms_resolved = true;
    g_takeover_save_manager = resolve_game_symbol(mth::sym::save_manager);
    g_takeover_slot_clear = resolve_game_symbol(mth::sym::save_slot_clear);
    g_takeover_init_gamestate = resolve_game_symbol(mth::sym::save_slot_init_gamestate);
    if (g_takeover_save_manager == 0 || g_takeover_slot_clear == 0 || g_takeover_init_gamestate == 0)
        logf(LogLevel::Warn, "takeover: g_saveManager/SaveSlot::Clear/InitGamestate not all resolved; a new save cannot be staged");
}

bool init_new_save_file(unsigned int slot)
{
    resolve_takeover_symbols();
    if (g_takeover_save_manager == 0 || g_takeover_slot_clear == 0 || g_takeover_init_gamestate == 0)
    {
        logf(LogLevel::Warn, "takeover: SaveSlot::Clear/InitGamestate or g_saveManager not resolved; cannot init a new save");
        return false;
    }
    const std::uintptr_t master = *reinterpret_cast<std::uintptr_t *>(g_takeover_save_manager + kSaveMasterOff);
    if (master == 0)
    {
        logf(LogLevel::Warn, "takeover: save master table not resolved; cannot init a new save");
        return false;
    }
    void *file_slot = reinterpret_cast<void *>(master + mth::layout::kSaveSlotArrayOff + static_cast<std::uintptr_t>(slot) * mth::layout::kSaveSlotStride);
    if (!pal::pointer_looks_valid(file_slot))
    {
        logf(LogLevel::Warn, "takeover: save slot %u address looks invalid; cannot init a new save", slot);
        return false;
    }
    auto clear_fn = reinterpret_cast<void (*)(void *, bool)>(g_takeover_slot_clear);
    auto init_fn = reinterpret_cast<void (*)(void *)>(g_takeover_init_gamestate);
    logf(LogLevel::Info, "takeover: initializing new save file (array slot %u at %p)", slot, file_slot);
    // false = fresh file, what the vanilla profile-select new-file sites pass; true is the NG+ cycle.
    // Going through the real address re-triggers the newfile-kit suppressor, so AP's starting kit wins.
    clear_fn(file_slot, false);
    init_fn(file_slot);
    return true;
}

} // namespace pal
