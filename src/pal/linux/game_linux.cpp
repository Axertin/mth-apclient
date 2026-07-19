#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>

#include "mth/core/ap/ap_ids.hpp"
#include "mth/core/data/game_layout.hpp"
#include "mth/core/data/game_symbols.hpp"
#include "mth/core/fountain_lamps.hpp"
#include "mth/core/shop_flatten.hpp"
#include "mth/core/sig_scan.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_hook.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_mem.hpp"
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

// ShopItem / ShopItemDef instance offsets (platform-independent class layout; not the InitState frame
// offsets above). Verified identical on the Windows build.
constexpr std::ptrdiff_t kShopItemDefOff = 0xf8;   // ShopItem -> active ShopItemDef*
constexpr std::ptrdiff_t kShopItemStockOff = 0xec; // ShopItem stock count; 0 renders the "sold out" box
constexpr std::ptrdiff_t kShopDefLocOff = 0x48;    // ShopItemDef cached GetCollectionIndex == loc_idx
constexpr std::ptrdiff_t kShopDefNextOff = 0x28;   // ShopItemDef -> next variant (level chain), null-terminated

pal::ShopLevelFn g_shop_stock_cb = nullptr;
pal::HookId g_shop_stock_hook = pal::kInvalidHookId;
void (*g_orig_shop_refresh)(void *) = nullptr;

pal::ShopFlattenFn g_shop_flatten_cb = nullptr;
pal::HookId g_shop_flatten_hook = pal::kInvalidHookId;
// 64-bit name hash; use a fixed-width type so Windows LLP64 (`unsigned long` == 32-bit) can't truncate it.
void *(*g_orig_shop_get)(std::uint64_t name_hash) = nullptr; // Shop::Get(uint64_t) -> ShopDef*

// ShopMenu::SetCursor post-hook: rewrites the selected box's name+description text from scouted AP data.
pal::ShopTextFn g_shop_text_cb = nullptr;
pal::HookId g_shop_text_hook = pal::kInvalidHookId;
void (*g_orig_set_cursor)(void *, int, bool) = nullptr;
void (*g_text_set_text)(void *textobj, const char *s, int len, unsigned int flags) = nullptr; // ycTextRenderObject::SetText
void (*g_text_set_color)(void *widget, std::uint32_t rgba) = nullptr;                         // ycTextComponent::SetColor

// ShopMenu instance fields (box list + selection); distinct from the InitState frame offsets above.
constexpr std::ptrdiff_t kShopNameWidgetOff = 0x148;
constexpr std::ptrdiff_t kShopDescWidgetOff = 0x150;
constexpr std::ptrdiff_t kShopBoxArrayOff = 0x1c8;
constexpr std::ptrdiff_t kShopBoxCountOff = 0x1d0;
constexpr std::ptrdiff_t kShopCursorOff = 0x1d8;
constexpr std::ptrdiff_t kShopBoxItemTypeOff = 0xcc; // ShopItem -> itemType (box's item kind)
constexpr std::ptrdiff_t kTextObjOff = 0x40;         // text widget -> ycTextRenderObject
constexpr int kShopSkipItemType = 0x65;

void repl_set_cursor(void *self, int index, bool b)
{
    if (g_orig_set_cursor)
        g_orig_set_cursor(self, index, b);
    if (g_shop_text_cb != nullptr && self != nullptr)
        g_shop_text_cb(self);
}

// A shop slot is a chain of ShopItemDef variants (one per level; rising price), linked via +0x28, each
// with its own loc_idx at +0x48. ShopMenu::SetupBoxes already advances the active variant (ShopItem+0xf8)
// past bought levels (gated on ShopItemDef+0x4d) before it calls Refresh, so we must NOT advance it again:
// re-advancing double-counts and skips a level per buy (#94). We only correct the stock count the suppressed
// grant no longer maintains -- set ShopItem+0xec to the number of unchecked AP levels, and 0 (sold out) once
// every level is checked. A slot with no AP-location levels (normal items, consumables) is left untouched.
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
            *reinterpret_cast<int *>(static_cast<char *>(self) + kShopItemStockOff) = remaining; // remaining unchecked levels
    }
    if (g_orig_shop_refresh)
        g_orig_shop_refresh(self);
}

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

// Items::IsItemCollected override lives in native_mod_entry.cpp (native modding hook; cross-platform).

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

// ---- new-file starting-kit suppression (zero the region-18 default upgrades after SaveSlot::Clear) ----
// SaveSlot upgrade-count fields written by SaveSlot::Clear on a fresh file (build 9b29bd0d, Linux):
constexpr std::ptrdiff_t kSparkUpgOff = 0x54;    // Spark_Upgrade   (itemType 0x46)
constexpr std::ptrdiff_t kHealthUpgOff = 0x130;  // Health_Upgrade  (itemType 0x45) bitfield (0xff = 8)
constexpr std::ptrdiff_t kMagicUpgOff = 0x170;   // Magic_Upgrade   (itemType 0x44)
constexpr std::ptrdiff_t kTrinketUpgOff = 0x950; // Trinket_Upgrade (itemType 0x48)
// Vials are not zeroed here: their bitfield offset drifts (#97) and App re-asserts the AP vial count via
// the mod API each tick anyway, overwriting the vanilla seed.

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
    pal::logf(pal::LogLevel::Info, "newfile-kit: zeroing default upgrades (health=%#x magic=%#x spark=%#x trinket=%#x)", field(kHealthUpgOff),
              field(kMagicUpgOff), field(kSparkUpgOff), field(kTrinketUpgOff));
    field(kHealthUpgOff) = 0;
    field(kMagicUpgOff) = 0;
    field(kSparkUpgOff) = 0;
    field(kTrinketUpgOff) = 0;
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
            if (!pal::pointer_looks_valid(slot))
                continue;
            auto *mask = reinterpret_cast<std::uint32_t *>(static_cast<char *>(slot) + kCheatMaskOff);
            std::uint32_t words[8];
            for (int i = 0; i < 8; ++i)
                words[i] = mask[i];
            g_seed_fn(slot_index, words);
            for (int i = 0; i < 8; ++i)
                mask[i] = words[i];
            // Diagnostic (#46): warp_home=121 -> word3 bit25, landing(ossex)=128 -> word4 bit0. Confirms
            // the force-on baseline landed in the mask (if inert in-game, it was seeded but not re-applied).
            pal::logf(pal::LogLevel::Debug, "modifiers: seeded cheat mask on slot=%p (slot_index=%d) warp_home[121]=%d landing[128]=%d", slot, slot_index,
                      (mask[3] >> 25) & 1, mask[4] & 1);
        }
    }
    if (g_orig_activate_slot)
        g_orig_activate_slot(self, flag);
}

void repl_activate_cheats(void *self)
{
    g_cheat_mgr = self; // capture the CheatManager singleton (not a player path)
    // Diagnostic (#46): each rebuild of the runtime cheat mirror from the mask. If this does NOT fire
    // after the flag=1 seed, our seeded force-on bits never go live.
    pal::logf(pal::LogLevel::Debug, "modifiers: ActivateSaveCheats fired (cheatmgr=%p)", self);
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

// ---- ability gating ----
// mth::Ability ordinals (kept as local constants so pal/ stays free of mth/ headers).
constexpr int kAbBurrow = 0;
constexpr int kAbSwim = 1;
constexpr int kAbRopeClimb = 2;
constexpr int kAbBouncePuff = 3;
constexpr int kAbBounceSpring = 4;
constexpr int kAbCarry = 5;
constexpr int kAbTrain = 6;

// Player-object offsets used by the detours (Linux build 828346d4).
constexpr std::ptrdiff_t kPlayerWaterListenerOff = 0x2c0; // WaterListener* (swim-vs-land discriminator)
constexpr std::ptrdiff_t kPlayerLowRoofFlagOff = 0x12f0;  // carry-disabled "low roof" pose flag (mirrors mth::layout::kPlayerLowRoofFlagOff)
// PhysicsContactPair -> colliding-entity component kind chain (shared by both CollideWith detours).
constexpr std::ptrdiff_t kContactEntityOff = 0x110;     // *(contactPair) + 0x110 -> entity
constexpr std::ptrdiff_t kEntityInteractCompOff = 0xa8; // entity + 0xa8 -> InteractComponent
constexpr std::ptrdiff_t kInteractKindOff = 0x6c;       // component + 0x6c -> int kind (8 == Player)
constexpr int kInteractKindPlayer = 8;
// TrainAuthority::OnNPCEvent case 0x15 selected-ticket-code chain; 100 = Exit (vanilla cancel).
constexpr unsigned kTrainDestPickEvent = 0x15;
// OnNPCEvent case 1 (interact/dialogue) and case 9 (TriggerRideDone/warp) carry the CTP boss ride gate.
constexpr unsigned kTrainInteractEvent = 1;
constexpr unsigned kTrainRideDoneEvent = 9;
constexpr std::ptrdiff_t kTrainAuthOwnerOff = 0x1b0; // this + 0x1b0 -> menu owner
constexpr std::ptrdiff_t kTrainMenuObjOff = 0xc8;    // owner + 0xc8 -> selection obj
constexpr std::ptrdiff_t kTrainSelCodeOff = 0x21c;   // obj + 0x21c -> int selected ticket itemType
constexpr int kTrainExitCode = 100;
// SaveSlot generic Train Pass (item 94) owned byte. Set by Items::OnPickupDone on collect; gates boarding
// (train presence) under train_rando. Platform data; not an mth/ layout offset.
constexpr std::ptrdiff_t kSaveTrainPassOwnedOff = 0x1c0;
// SaveSlot train-present byte (platform data; not an mth/ layout offset).
constexpr std::ptrdiff_t kSaveTrainPresentOff = 0x1c1;
// SaveSlot unlocked-train-lines bitfield (5 low bits, one per destination). Set by the TrainAuthority ctor
// on station footfall; the AP client clamps it to the granted-ticket mask. Same layout on both platforms.
constexpr std::ptrdiff_t kSaveTrainUnlockedLinesOff = 0x1e0;
// CTP boss (Thorne 2) defeated bit: byte +0x281 mask 0x02 of the SaveSlot+0x280 boss bitfield. The Coltrane
// line ride is gated on it (#108).
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

// Resolved targets (cached by abilities_available).
std::uintptr_t g_addr_burrow_ground = 0;
std::uintptr_t g_addr_rope_climb = 0;
std::uintptr_t g_addr_bounce_plant = 0;
std::uintptr_t g_addr_bounce_launch = 0;
std::uintptr_t g_addr_spring = 0;
std::uintptr_t g_addr_pickup = 0;
std::uintptr_t g_addr_train_npc = 0;
std::uintptr_t g_addr_burrow_jump = 0; // #56

pal::HookId g_id_burrow = pal::kInvalidHookId;
pal::HookId g_id_rope = pal::kInvalidHookId;
pal::HookId g_id_puff = pal::kInvalidHookId;
pal::HookId g_id_launch = pal::kInvalidHookId;
pal::HookId g_id_spring = pal::kInvalidHookId;
pal::HookId g_id_carry = pal::kInvalidHookId;
pal::HookId g_id_train = pal::kInvalidHookId;
pal::HookId g_id_burrow_jump = pal::kInvalidHookId; // #56

unsigned long (*g_orig_burrow_ground)(void *) = nullptr;
char (*g_water_is_deep)(void *, bool) = nullptr; // WaterListener::IsInDeepWaterInternal(wl, false)
void (*g_orig_rope_climb)(void *, void *, bool, bool) = nullptr;
void (*g_orig_bounce_plant)(void *, void *) = nullptr;
void (*g_orig_bounce_launch)(void *, void *) = nullptr;
void (*g_orig_spring)(void *, void *) = nullptr;
unsigned long (*g_orig_pickup)(void *, bool, bool, bool) = nullptr;
void (*g_orig_train_npc)(void *, unsigned, void *) = nullptr;
void (*g_orig_burrow_jump)(void *) = nullptr; // #56
// True only across Mina::OnBurrowJump, whose emerge auto-grabs via PickUpAnyNearbyCarryableObject: without
// it, repl_pickup would set the duck-pose on every carry-disabled emerge (a spurious hop).
bool g_in_burrow_emerge = false;

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

// Out-of-line launch for ground/burrow-underable puffs (player is the second arg, always the launchee);
// the floating case bounces inline in CollideWith above (issue #47).
void repl_bounce_launch(void *self, void *player)
{
    if (ability_blocked(kAbBouncePuff))
        return;
    if (g_orig_bounce_launch)
        g_orig_bounce_launch(self, player);
}

void repl_spring(void *self, void *contact_pair)
{
    if (collider_is_player(contact_pair) && ability_blocked(kAbBounceSpring))
        return;
    if (g_orig_spring)
        g_orig_spring(self, contact_pair);
}

// Blocked: set the low-roof pose flag so the engine presents the ducked-under-roof render instead of popping
// up; skip it during a burrow emerge (g_in_burrow_emerge), where no roof is overhead.
unsigned long repl_pickup(void *self, bool a, bool b, bool c)
{
    if (self != nullptr && ability_blocked(kAbCarry))
    {
        if (!g_in_burrow_emerge)
            *reinterpret_cast<char *>(static_cast<char *>(self) + kPlayerLowRoofFlagOff) = 1;
        return 0;
    }
    return g_orig_pickup ? g_orig_pickup(self, a, b, c) : 0;
}

// #56: no native "duck under a carryable" exists (the burrow-emerge roof/snap handles only low map
// ceilings), so with carry disabled we detect a carryable in grab range and suppress the emerge, leaving
// Mina burrowed beneath it. carryable_overhead is a read-only replica of the game's own grab query.
// Offsets from Player::PickUpAnyNearbyCarryableObject (0xd515b0, build 8de7a6b5).
struct ycAABB
{
    float v[6]; // center.xyz [0..2], halfExtent.xyz [3..5]
};
void (*g_get_aabb)(ycAABB *, void *, bool, unsigned) = nullptr;                     // PhysicsComponent::GetAABB (sret)
void *(*g_get_closest)(void *, ycAABB, int, float, int *, unsigned long) = nullptr; // CarryManager::GetClosestCarryableObject

bool carryable_overhead(void *self)
{
    if (self == nullptr || g_get_aabb == nullptr || g_get_closest == nullptr)
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

    ycAABB box{};
    g_get_aabb(&box, phys, false, 0u);
    // The game clamps the box's z half-extent to |comp+0xd8| * 1.6 * 0.3 before the query.
    const float fd8 = *reinterpret_cast<float *>(static_cast<char *>(comp) + 0xd8);
    const float clamp = (fd8 < 0.0f ? -fd8 : fd8) * 1.6f * 0.3f;
    if (clamp < box.v[5])
        box.v[5] = clamp;

    int out_n = 0;
    return g_get_closest(mgr, box, layer, 1.6f, &out_n, 0ul) != nullptr; // radius 1.6, mask 0 (emerge args)
}

void repl_burrow_jump(void *self)
{
    if (self != nullptr && ability_blocked(kAbCarry) && carryable_overhead(self))
    {
        pal::logf(pal::LogLevel::Debug, "carry: burrow-emerge suppressed (carryable overhead)");
        return;
    }
    if (g_orig_burrow_jump)
    {
        g_in_burrow_emerge = true; // keep repl_pickup's duck-pose out of the emerge auto-grab
        g_orig_burrow_jump(self);
        g_in_burrow_emerge = false;
    }
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

// One-time .text patch of ShopMenu::SetupBoxes' hardcoded "always-shown" train-line mask. The menu shows a
// ticket box when its itemType is in a hardcoded set (mask 0x23 = lines 94/95/99) OR its SaveSlot+0x1e0 bit
// is set. That hardcode makes Coltrane Peak (99) selectable regardless of ticket, defeating the +0x1e0 clamp.
// Drop the mask to 0x03 so only 94 (board) and 95 (Ossex/HUB) stay unconditional and 96-99 gate on +0x1e0
// (driven by enforce_train_destinations). #98.
void patch_train_destination_menu()
{
    const pal::ModuleInfo gm = pal::game_module();
    if (gm.base == 0 || gm.size == 0)
    {
        pal::logf(pal::LogLevel::Warn, "train: game module unavailable; SetupBoxes mask patch skipped (99 stays always-shown)");
        return;
    }
    // Anchor: lea eax,[rcx-0x5e]; cmp eax,5; ja +0xa; mov edx,0x23; bt edx,eax  (0x23 immediate at index 9).
    static const std::uint8_t pat[] = {0x8D, 0x41, 0xA2, 0x83, 0xF8, 0x05, 0x77, 0x0A, 0xBA, 0x23, 0x00, 0x00, 0x00, 0x0F, 0xA3, 0xC2};
    static const std::uint8_t msk[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    constexpr std::size_t kMaskByte = 9;
    const std::span<const std::uint8_t> region{reinterpret_cast<const std::uint8_t *>(gm.base), gm.size};
    const mth::sig::Match m = mth::sig::find_masked(region, pat, msk, sizeof(pat));
    if (!m.found || !m.unique)
    {
        pal::logf(pal::LogLevel::Warn, "train: SetupBoxes mask site %s; Coltrane may stay always-shown", m.found ? "ambiguous" : "not found");
        return;
    }
    auto *site = reinterpret_cast<std::uint8_t *>(gm.base + m.offset + kMaskByte);
    if (*site != 0x23)
    {
        pal::logf(pal::LogLevel::Warn, "train: SetupBoxes mask byte=0x%02x (expected 0x23); patch skipped", *site);
        return;
    }
    const std::uint8_t patched = 0x03;
    if (!pal::patch_code(site, &patched, 1))
    {
        pal::logf(pal::LogLevel::Error, "train: SetupBoxes mask patch_code failed");
        return;
    }
    pal::logf(pal::LogLevel::Info, "train: SetupBoxes mask 0x23->0x03 at 0x%llx (un-ticketed Coltrane now non-selectable)",
              static_cast<unsigned long long>(gm.base + m.offset + kMaskByte));
}

// Pawnty (PawnShopNPC::OnNPCEvent) disable. event 0x1f is InteractComponent::IsInteractable's veto
// query: a nonzero float at info+0x8 means "not interactable" -> no prompt. No-op everything else so
// no dialogue line is set and the sell menu never opens.
constexpr unsigned kPawnInteractableQueryEvent = 0x1f;
constexpr std::ptrdiff_t kPawnVetoFloatOff = 0x8; // InteractEventInfo veto float
pal::PawnShopBlockFn g_pawn_disable;
pal::HookId g_pawn_hook = pal::kInvalidHookId;
void (*g_orig_pawn_npc)(void *, unsigned, void *) = nullptr;

void repl_pawn_npc(void *self, unsigned event, void *info)
{
    if (g_pawn_disable && g_pawn_disable())
    {
        if (event == kPawnInteractableQueryEvent && info != nullptr)
            *reinterpret_cast<float *>(static_cast<char *>(info) + kPawnVetoFloatOff) = 1.0f;
        return; // swallow dialogue/menu/can-interact; never call the original
    }
    if (g_orig_pawn_npc)
        g_orig_pawn_npc(self, event, info);
}

// HubFountain::Bulb::Update(float dt, bool lit): forces lit=true for AP-granted generator lamps.
// Prototype order (self, dt, lit) matches the mangled Update(float,bool) on both ABIs; never reorder.
pal::FountainLampFn g_fountain_mask_fn;
pal::HookId g_fountain_hook = pal::kInvalidHookId;
void (*g_orig_bulb_update)(void *, float, bool) = nullptr;

void repl_bulb_update(void *self, float dt, bool lit)
{
    if (g_fountain_mask_fn && self != nullptr)
    {
        const std::uint32_t idx = *reinterpret_cast<std::uint32_t *>(static_cast<char *>(self) + mth::layout::kBulbIndexOff);
        if (idx < static_cast<std::uint32_t>(mth::kGeneratorLampCount) && ((g_fountain_mask_fn() >> idx) & 1u))
            lit = true; // force this generator lamp lit; never writes SaveSlot+0x290
    }
    if (g_orig_bulb_update)
        g_orig_bulb_update(self, dt, lit);
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

bool current_room_index(void *room_manager, std::uint32_t *out)
{
    if (room_manager == nullptr)
        return false;
    // Room index field; live build 828346d4 = +0x1b4 (stale Ghidra build read +0x1bc). Re-verify on update.
    const std::int32_t idx = *reinterpret_cast<const std::int32_t *>(static_cast<const char *>(room_manager) + 0x1b4);
    if (idx < 0)
        return false;
    *out = static_cast<std::uint32_t>(idx);
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
    void *slot = *reinterpret_cast<void **>(save_manager_global + 0x18); // SaveSlot* = *(g_saveManager + 0x18)
    // Title/menu init leaves this uninitialized (non-pointer); every caller null-checks, so fail closed.
    return pal::pointer_looks_valid(slot) ? slot : nullptr;
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

bool install_pawn_shop_hook(PawnShopBlockFn disable)
{
    g_pawn_disable = std::move(disable);
    const std::uintptr_t addr = resolve_game_symbol(mth::sym::pawn_shop_on_npc_event);
    if (addr == 0)
    {
        logf(LogLevel::Warn, "pawnty: PawnShopNPC::OnNPCEvent not resolved; pawn-shop disable off");
        g_pawn_disable = nullptr;
        return false;
    }
    g_pawn_hook =
        hook_engine().install_hook(reinterpret_cast<void *>(addr), reinterpret_cast<void *>(&repl_pawn_npc), reinterpret_cast<void **>(&g_orig_pawn_npc));
    if (g_pawn_hook == kInvalidHookId)
    {
        logf(LogLevel::Error, "pawnty: failed to hook PawnShopNPC::OnNPCEvent");
        g_pawn_disable = nullptr;
        return false;
    }
    logf(LogLevel::Info, "pawnty: hooked PawnShopNPC::OnNPCEvent (id=%llu)", static_cast<unsigned long long>(g_pawn_hook));
    return true;
}

void remove_pawn_shop_hook()
{
    if (g_pawn_hook != kInvalidHookId)
        hook_engine().remove_hook(g_pawn_hook);
    g_pawn_hook = kInvalidHookId;
    g_pawn_disable = nullptr;
}

bool install_fountain_lamp_hook(FountainLampFn lit_mask)
{
    g_fountain_mask_fn = std::move(lit_mask);
    const std::uintptr_t addr = resolve_game_symbol(mth::sym::hub_fountain_bulb_update);
    if (addr == 0)
    {
        logf(LogLevel::Warn, "fountain: HubFountain::Bulb::Update not resolved; lamp pre-light off");
        g_fountain_mask_fn = nullptr;
        return false;
    }
    g_fountain_hook =
        hook_engine().install_hook(reinterpret_cast<void *>(addr), reinterpret_cast<void *>(&repl_bulb_update), reinterpret_cast<void **>(&g_orig_bulb_update));
    if (g_fountain_hook == kInvalidHookId)
    {
        logf(LogLevel::Error, "fountain: failed to hook HubFountain::Bulb::Update");
        g_fountain_mask_fn = nullptr;
        return false;
    }
    logf(LogLevel::Info, "fountain: hooked HubFountain::Bulb::Update (id=%llu)", static_cast<unsigned long long>(g_fountain_hook));
    return true;
}

void remove_fountain_lamp_hook()
{
    if (g_fountain_hook != kInvalidHookId)
        hook_engine().remove_hook(g_fountain_hook);
    g_fountain_hook = kInvalidHookId;
    g_fountain_mask_fn = nullptr;
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
    if (shop_menu == nullptr)
        return -1;
    void **boxes = *reinterpret_cast<void ***>(static_cast<char *>(shop_menu) + kShopBoxArrayOff);
    const int count = *reinterpret_cast<int *>(static_cast<char *>(shop_menu) + kShopBoxCountOff);
    const int cursor = *reinterpret_cast<int *>(static_cast<char *>(shop_menu) + kShopCursorOff);
    if (boxes == nullptr || cursor < 0 || cursor >= count)
        return -1;
    void *box = boxes[cursor];
    if (box == nullptr)
        return -1;
    const int item_type = *reinterpret_cast<int *>(static_cast<char *>(box) + kShopBoxItemTypeOff);
    if (item_type == kShopSkipItemType)
        return -1;
    // Sold-out box: stock count 0 (same field repl_shop_refresh zeroes for a fully-bought slot).
    if (*reinterpret_cast<int *>(static_cast<char *>(box) + kShopItemStockOff) == 0)
        return -1;
    void *def = *reinterpret_cast<void **>(static_cast<char *>(box) + kShopItemDefOff);
    if (def == nullptr)
        return -1;
    return *reinterpret_cast<int *>(static_cast<char *>(def) + kShopDefLocOff);
}

void shop_enumerate_locs(void *shop_menu, void (*sink)(int loc, void *ctx), void *ctx)
{
    if (shop_menu == nullptr || sink == nullptr)
        return;
    void **boxes = *reinterpret_cast<void ***>(static_cast<char *>(shop_menu) + kShopBoxArrayOff);
    const int count = *reinterpret_cast<int *>(static_cast<char *>(shop_menu) + kShopBoxCountOff);
    if (boxes == nullptr)
        return;
    for (int i = 0; i < count; ++i)
    {
        void *box = boxes[i];
        if (box == nullptr)
            continue;
        void *def = *reinterpret_cast<void **>(static_cast<char *>(box) + kShopItemDefOff);
        if (def == nullptr)
            continue;
        sink(*reinterpret_cast<int *>(static_cast<char *>(def) + kShopDefLocOff), ctx);
    }
}

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
    if (widget == nullptr || utf8 == nullptr || g_text_set_text == nullptr)
        return;
    g_text_set_text(static_cast<char *>(widget) + kTextObjOff, utf8, 0, 0);
}

void shop_set_color(void *widget, std::uint32_t rgba)
{
    if (widget == nullptr || g_text_set_color == nullptr)
        return;
    g_text_set_color(widget, rgba);
}

bool install_shop_text_hook(ShopTextFn on_set_cursor)
{
    const std::uintptr_t addr = resolve_game_symbol(mth::sym::shop_set_cursor);
    g_text_set_text = reinterpret_cast<void (*)(void *, const char *, int, unsigned int)>(resolve_game_symbol(mth::sym::text_set_text));
    g_text_set_color = reinterpret_cast<void (*)(void *, std::uint32_t)>(resolve_game_symbol(mth::sym::text_set_color));
    if (addr == 0 || g_text_set_text == nullptr || g_text_set_color == nullptr)
    {
        logf(LogLevel::Warn, "shop: SetCursor/SetText/SetColor not fully resolved (cursor=0x%llx text=%d color=%d); shop text override disabled",
             static_cast<unsigned long long>(addr), static_cast<int>(g_text_set_text != nullptr), static_cast<int>(g_text_set_color != nullptr));
        g_text_set_text = nullptr;
        g_text_set_color = nullptr;
        return false;
    }
    g_shop_text_cb = on_set_cursor;
    g_shop_text_hook =
        hook_engine().install_hook(reinterpret_cast<void *>(addr), reinterpret_cast<void *>(&repl_set_cursor), reinterpret_cast<void **>(&g_orig_set_cursor));
    if (g_shop_text_hook == kInvalidHookId)
    {
        logf(LogLevel::Error, "shop: failed to hook ShopMenu::SetCursor");
        g_shop_text_cb = nullptr;
        g_text_set_text = nullptr;
        g_text_set_color = nullptr;
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
    g_text_set_text = nullptr;
    g_text_set_color = nullptr;
}

// install_item_collected_hook / remove_item_collected_hook live in mod/mod_api.cpp.

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
    if (!pal::pointer_looks_valid(aslot) && !pal::pointer_looks_valid(lslot))
    {
        logf(LogLevel::Warn, "modifiers: live set idx=%d failed (no valid save slot active)", idx);
        return false;
    }
    set_mask_bit(aslot, idx, on);
    if (lslot != aslot)
        set_mask_bit(lslot, idx, on);
    if (!pal::pointer_looks_valid(aslot) || !pal::pointer_looks_valid(lslot))
        logf(LogLevel::Warn, "modifiers: live set idx=%d partial (apply=%p live=%p)", idx, aslot, lslot);
    // ActivateSaveCheats reads the apply-path slot internally, so only rebuild when it is valid.
    if (pal::pointer_looks_valid(aslot) && g_cheat_mgr != nullptr && g_orig_activate_cheats != nullptr)
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

// ---- capacity upgrades ----
// Per-upgrade SaveSlot field (index Magic,Health,Spark,Vial,Trinket); popcount = capacity. These
// SaveSlot offsets match on both platforms; UpdateStats recomputes the live maxima from them.
// Build-specific: re-verify against the shipping build. The Vial slot (kVialUpgradeIndex) is a placeholder
// and never written: vials go through the mod API (see App).
namespace
{
constexpr std::ptrdiff_t kUpgradeFieldOff[5] = {0x170, 0x130, 0x54, 0x18c, 0x950};

// Live resource-pool fields, so a capacity grant can keep the missing amount constant instead of
// leaving current untouched (build 9b29bd0d). CombatCore = *(Player+0x130). See the re-note
// 2026-06-24-resource-current-max-fields. Trinket has no depleting pool and is skipped.
constexpr std::ptrdiff_t kCombatCoreOff = 0x130; // Player -> CombatCore*
constexpr std::ptrdiff_t kHpCurOff = 0x1e0;      // CombatCore, float
constexpr std::ptrdiff_t kHpMaxOff = 0x1e8;      // CombatCore, float
// Joules (magic), pinned by the game's own accessors: PlayerGetJoules/PlayerSetJoules use Player+0x117c,
// and UpdateStats writes the max at Player+0x1180. These read 0x1174/0x1178 until r148851; those are the
// backup-sidearm and latched in-flight sidearm itemTypes, so every grant clamped one sidearm itemType
// against the other and the backup slot emptied on the next WriteSave.
constexpr std::ptrdiff_t kMagicCurOff = 0x117c; // Player, int
constexpr std::ptrdiff_t kMagicMaxOff = 0x1180; // Player, int
constexpr std::ptrdiff_t kSparkCurOff = 0x50;   // SaveSlot, int
constexpr std::ptrdiff_t kSparkMaxOff = 0x230;  // CombatCore, int
// Vials are NOT written here: their SaveSlot bitfield offset drifts between builds (#97), so App drives
// them through the offset-free mod-API accessors (mod::set_player_max_vials) instead.

bool g_up_resolved = false;
bool g_up_ok = false;
bool g_up_layout_ok = true; // cleared permanently if an upgrade field reads out of its plausible domain
std::uintptr_t g_up_save_manager = 0;
void (*g_up_update_stats)(void *) = nullptr; // Player::UpdateStats(this)

float fld_f(void *base, std::ptrdiff_t off)
{
    return *reinterpret_cast<float *>(static_cast<char *>(base) + off);
}
int fld_i(void *base, std::ptrdiff_t off)
{
    return *reinterpret_cast<int *>(static_cast<char *>(base) + off);
}
} // namespace

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
    // Diagnostic (#46): called every tick while dirty, so log only on outcome CHANGE. Identifies which
    // silent guard drops the new-file start-inventory grant.
    static int s_last_outcome = -1;
    auto trace = [&](int outcome, const char *what, void *slot)
    {
        if (outcome == s_last_outcome)
            return;
        s_last_outcome = outcome;
        pal::logf(pal::LogLevel::Debug, "upgrades: apply -> %s (player=%p slot=%p counts=[%d,%d,%d,%d,%d])", what, player, slot, counts[0], counts[1],
                  counts[2], counts[3], counts[4]);
    };
    if (!upgrades_available())
    {
        trace(1, "skip: symbols unavailable", nullptr);
        return false;
    }
    if (player == nullptr)
    {
        trace(2, "skip: player null", nullptr);
        return false;
    }
    if (!g_up_layout_ok)
    {
        trace(3, "skip: layout disabled", nullptr);
        return false;
    }
    void *slot = active_save_slot(g_up_save_manager);
    if (slot == nullptr)
    {
        trace(4, "skip: active SaveSlot* null", slot);
        return false;
    }
    void *cc = *reinterpret_cast<void **>(static_cast<char *>(player) + kCombatCoreOff);

    // Capture the missing amount of each pool before the grant; UpdateStats raises the max but never
    // refills current, so we restore the same missing afterward (new_current = new_max - old_missing).
    // No-op on a resend (max unchanged) and a fresh AP file starts full (missing 0).
    const float hp_missing = cc != nullptr ? fld_f(cc, kHpMaxOff) - fld_f(cc, kHpCurOff) : 0.0f;
    const int magic_missing = fld_i(player, kMagicMaxOff) - fld_i(player, kMagicCurOff);
    const int spark_missing = cc != nullptr ? fld_i(cc, kSparkMaxOff) - fld_i(slot, kSparkCurOff) : 0;

    for (int i = 0; i < 5; ++i)
    {
        if (i == mth::kVialUpgradeIndex)
            continue; // vials are applied via the mod API (offset-free), not this bitfield
        auto &fieldv = *reinterpret_cast<std::uint32_t *>(static_cast<char *>(slot) + kUpgradeFieldOff[i]);
        if (!mth::upgrade_field_in_domain(i, fieldv))
        {
            g_up_layout_ok = false; // upgrades_available()-adjacent guard now short-circuits future calls
            logf(LogLevel::Warn, "upgrades: kUpgradeFieldOff[%d] read=0x%x exceeds cap %d; offset may have shifted, upgrade writes DISABLED", i, fieldv,
                 mth::kUpgradeCaps[i]);
            return false;
        }
        fieldv = mth::upgrade_field_value(i, counts[i], fieldv);
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

    trace(0, "applied", slot);
    return true;
}

bool abilities_available()
{
    if (g_ab_resolved)
        return g_ab_ok;
    g_ab_resolved = true;
    g_addr_burrow_ground = resolve_game_symbol(mth::sym::player_set_burrow_ground);
    g_water_is_deep = reinterpret_cast<char (*)(void *, bool)>(resolve_game_symbol(mth::sym::water_is_in_deep_water));
    g_addr_rope_climb = resolve_game_symbol(mth::sym::player_rope_climb_start);
    g_addr_bounce_plant = resolve_game_symbol(mth::sym::bounce_plant_collide);
    g_addr_bounce_launch = resolve_game_symbol(mth::sym::bounce_plant_launch);
    g_addr_spring = resolve_game_symbol(mth::sym::spring_bellows_collide);
    g_addr_pickup = resolve_game_symbol(mth::sym::player_pickup_carryable);
    g_addr_train_npc = resolve_game_symbol(mth::sym::train_authority_on_npc_event);
    g_addr_burrow_jump = resolve_game_symbol(mth::sym::mina_on_burrow_jump); // #56
    g_get_aabb = reinterpret_cast<void (*)(ycAABB *, void *, bool, unsigned)>(resolve_game_symbol(mth::sym::physics_get_aabb));
    g_get_closest = reinterpret_cast<void *(*)(void *, ycAABB, int, float, int *, unsigned long)>(resolve_game_symbol(mth::sym::carry_get_closest));
    g_ab_ok = g_addr_burrow_ground != 0 || g_addr_rope_climb != 0 || g_addr_bounce_plant != 0 || g_addr_bounce_launch != 0 || g_addr_spring != 0 ||
              g_addr_pickup != 0 || g_addr_train_npc != 0;
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

    if (g_addr_bounce_launch != 0)
    {
        g_id_launch = hook_engine().install_hook(reinterpret_cast<void *>(g_addr_bounce_launch), reinterpret_cast<void *>(&repl_bounce_launch),
                                                 reinterpret_cast<void **>(&g_orig_bounce_launch));
        if (g_id_launch == kInvalidHookId)
            logf(LogLevel::Error, "abilities: failed to hook BouncePlant::BounceLaunch");
    }
    else
        logf(LogLevel::Warn, "abilities: BouncePlant::BounceLaunch not resolved; ground-puff gating disabled");

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

    if (g_addr_burrow_jump != 0) // #56: OnBurrowJump: suppress emerge under a carryable
    {
        g_id_burrow_jump = hook_engine().install_hook(reinterpret_cast<void *>(g_addr_burrow_jump), reinterpret_cast<void *>(&repl_burrow_jump),
                                                      reinterpret_cast<void **>(&g_orig_burrow_jump));
        if (g_id_burrow_jump == kInvalidHookId)
            logf(LogLevel::Warn, "abilities: failed to hook Mina::OnBurrowJump (#56)");
    }
    else
        logf(LogLevel::Warn, "abilities: Mina::OnBurrowJump not resolved; carry emerge-suppress disabled");

    const bool any = g_id_burrow != kInvalidHookId || g_id_rope != kInvalidHookId || g_id_puff != kInvalidHookId || g_id_launch != kInvalidHookId ||
                     g_id_spring != kInvalidHookId || g_id_carry != kInvalidHookId || g_id_train != kInvalidHookId || g_id_burrow_jump != kInvalidHookId;
    if (any)
        logf(LogLevel::Info, "abilities: ability gating hooks installed");
    else
        g_ability_block = nullptr;
    return any;
}

void remove_ability_hooks()
{
    for (HookId *id : {&g_id_burrow, &g_id_rope, &g_id_puff, &g_id_launch, &g_id_spring, &g_id_carry, &g_id_train, &g_id_burrow_jump})
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

void enforce_train_destinations(std::uintptr_t save_manager_global, std::uint32_t line_mask)
{
    void *slot = active_save_slot(save_manager_global);
    if (slot == nullptr)
        return;
    // Unlocked-lines bitfield is a byte at +0x1e0 (5 low bits); the footfall unlock only ORs bits in, so
    // writing the granted mask each frame clears any line the game auto-unlocked on a station visit.
    *reinterpret_cast<std::uint8_t *>(static_cast<char *>(slot) + kSaveTrainUnlockedLinesOff) = static_cast<std::uint8_t>(line_mask & 0xffu);
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

} // namespace pal
