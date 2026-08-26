#include <algorithm>
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
#include "mth/core/data/cheat_mask.hpp"
#include "mth/core/data/game_layout.hpp"
#include "mth/core/data/game_symbols.hpp"
#include "mth/core/sig_scan.hpp"
#include "mth/core/title_menu.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_hook.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_mem.hpp"
#include "pal/pal_module.hpp"

namespace
{
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

// Items::IsItemCollected override lives in native_mod_entry.cpp (native modding hook; cross-platform).

// ---- modifier control ----
constexpr std::ptrdiff_t kCheatMaskOff = 0xcb0; // 8 u32 words: per-save enable bitmask
constexpr std::ptrdiff_t kApplySlotOff = 0x08;  // g_saveManager+0x08 = apply-path slot (garbage on build 9cd1468c)
constexpr std::ptrdiff_t kLiveSlotOff = 0x18;   // g_saveManager+0x18 = live-gameplay slot (the real one)
constexpr std::ptrdiff_t kSlotIndexOff = 0x20;  // g_saveManager+0x20 = active 0-based slot index (confirmed in-game 9cd1468c)
constexpr std::ptrdiff_t kSaveMasterOff = 0x28; // g_saveManager+0x28 = SaveMasterData, holding the vanilla slot array

std::uintptr_t g_mod_save_manager = 0; // resolved g_saveManager
std::uintptr_t g_addr_activate_slot = 0;
std::uintptr_t g_addr_toggle = 0;
std::uintptr_t g_addr_set_applied = 0;
bool g_mod_resolved = false;
bool g_mod_ok = false;

pal::SeedFn g_seed_fn;
pal::SaveLoadedFn g_save_loaded_fn;
pal::BlockFn g_block_fn;

pal::HookId g_id_activate_slot = pal::kInvalidHookId;
void (*g_orig_activate_slot)(void *, bool) = nullptr;
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

// CheatManager layout (distinct from the SaveSlot mask above). +0x01 is an "activated" bool that
// every read site checks before consulting the mask, and +0x20 holds a u64[4] covering indices
// 0..63, 64..127, 128..191, 192..255. CheatManager::ActivateSaveCheats writes that byte to 1 at the
// top of its body and is its only writer, so a set byte means the game has activated a save slot.
constexpr std::size_t kCheatMgrActivatedOff = 0x01;
constexpr std::size_t kCheatMgrMaskOff = 0x20;

void *g_cheat_mgr_sym = nullptr;
bool g_cheat_mgr_outcome_logged = false; // one-shot: the manager resolved and validated
bool g_cheat_mgr_miss_logged = false;    // one-shot: symbol not (yet) resolved

// A validated CheatManager, resolved once so a caller needing both the activated byte and the mask
// does not walk +0x20 twice. A null base means there is no usable manager.
struct CheatManagerView
{
    std::uint8_t *base = nullptr;
    std::uint64_t *mask = nullptr;
};

// A candidate CheatManager* is trusted only if it looks like a real, naturally aligned pointer, its
// "activated" byte reads as an actual bool, and the mask pointer it carries at +0x20 is itself a
// valid, aligned pointer.
// pal::pointer_looks_valid is a range test, not a mapping check (pal/pal_mem.hpp), so alignment and
// the layout's own field values catch a candidate that merely landed in the valid range.
std::uint64_t *cheat_mask_at(void *candidate)
{
    const auto addr = reinterpret_cast<std::uintptr_t>(candidate);
    if (!pal::pointer_looks_valid(candidate) || (addr & 7u) != 0)
        return nullptr;
    auto *base = reinterpret_cast<std::uint8_t *>(candidate);
    if (base[kCheatMgrActivatedOff] > 1)
        return nullptr;
    auto *mask = *reinterpret_cast<std::uint64_t **>(base + kCheatMgrMaskOff);
    const auto mask_addr = reinterpret_cast<std::uintptr_t>(mask);
    if (!pal::pointer_looks_valid(mask) || (mask_addr & 7u) != 0)
        return nullptr;
    return mask;
}

// Resolves g_cheatManager through the game's own GetSymAddr. Only a successful resolve is cached:
// a miss is retried on every call, because a caller asking this from its own constructor can race
// the App constructor body publishing the game's text range (mod::sym_addr fails closed until
// then), and caching that transient miss would make traps look permanently unavailable for the rest
// of the session. The "not yet available" log is still one-shot, so a slow race does not spam it.
void ensure_cheat_mgr_symbol()
{
    if (g_cheat_mgr_sym != nullptr)
        return;
    g_cheat_mgr_sym = mod::sym_addr("g_cheatManager");
    if (g_cheat_mgr_sym != nullptr)
    {
        pal::logf(pal::LogLevel::Info, "traps: g_cheatManager resolved");
        return;
    }
    if (!g_cheat_mgr_miss_logged)
    {
        g_cheat_mgr_miss_logged = true;
        pal::logf(pal::LogLevel::Warn, "traps: g_cheatManager NOT available (yet)");
    }
}

// GetSymAddr hands back the CheatManager itself, already dereferenced: the symbol table's entry for
// g_cheatManager loads the global rather than taking its address. The validation below still earns
// its place, since cheat_mask_at is all that stands between a resolve gone wrong and a write into
// arbitrary game memory. The outcome logs once, since a human reads that line in-game.
CheatManagerView cheat_manager()
{
    ensure_cheat_mgr_symbol();
    if (g_cheat_mgr_sym == nullptr)
        return {};
    std::uint64_t *mask = cheat_mask_at(g_cheat_mgr_sym);
    if (mask == nullptr)
        return {};
    if (!g_cheat_mgr_outcome_logged)
    {
        g_cheat_mgr_outcome_logged = true;
        pal::logf(pal::LogLevel::Info, "traps: g_cheatManager resolved (mask at +0x20 looked valid)");
    }
    return {reinterpret_cast<std::uint8_t *>(g_cheat_mgr_sym), mask};
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
    // Post-original: the slot's contents are the run's by now. flag=false is a title/profile-menu
    // re-activation and must not fire, or the notify lands before the save data does.
    if (flag && g_save_loaded_fn)
        g_save_loaded_fn();
}

void repl_toggle(void *self, int idx, bool enable, void *slot, bool b, int i)
{
    const bool blocked = g_block_fn && g_block_fn(idx);
    pal::logf(pal::LogLevel::Debug, "modifiers: ToggleCheat idx=%d enable=%d -> %s", idx, static_cast<int>(enable), blocked ? "BLOCKED" : "allowed");
    if (blocked)
        return;
    if (g_orig_toggle)
        g_orig_toggle(self, idx, enable, slot, b, i);
}

void repl_set_applied(void *self, int idx, bool applied, void *slot)
{
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
    pal::boneup_annotate_description(self);
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

// Pending bounce target (3 floats, FLT_MAX when none) and the bone-bounce variant marker. Player::OnBounce
// consumes both; its own early-out clears them, and the block path has to do the same or the target stays
// armed and fires late (#168).
constexpr std::ptrdiff_t kPlayerBounceTargetOff = 0x252c;
constexpr std::ptrdiff_t kPlayerBounceMarkerOff = 0x24c0;
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
std::uintptr_t g_addr_on_bounce = 0;
std::uintptr_t g_addr_spring = 0;
std::uintptr_t g_addr_spring_listener = 0;
std::uintptr_t g_addr_pickup = 0;
std::uintptr_t g_addr_train_npc = 0;
std::uintptr_t g_addr_burrow_jump = 0; // #56

pal::HookId g_id_burrow = pal::kInvalidHookId;
pal::HookId g_id_rope = pal::kInvalidHookId;
pal::HookId g_id_puff = pal::kInvalidHookId;
pal::HookId g_id_launch = pal::kInvalidHookId;
pal::HookId g_id_on_bounce = pal::kInvalidHookId;
pal::HookId g_id_spring = pal::kInvalidHookId;
pal::HookId g_id_spring_listener = pal::kInvalidHookId;
pal::HookId g_id_carry = pal::kInvalidHookId;
pal::HookId g_id_train = pal::kInvalidHookId;
pal::HookId g_id_burrow_jump = pal::kInvalidHookId; // #56

unsigned long (*g_orig_burrow_ground)(void *) = nullptr;
void (*g_orig_rope_climb)(void *, void *, bool, bool) = nullptr;
void (*g_orig_bounce_plant)(void *, void *) = nullptr;
void (*g_orig_bounce_launch)(void *, void *) = nullptr;
void (*g_orig_on_bounce)(void *) = nullptr;
void (*g_orig_spring)(void *, void *) = nullptr;
void (*g_orig_spring_listener)(void *, void *) = nullptr;
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

// Shared by both SpringBellows::CollideWith bodies (see sym::spring_bellows_collide_listener). The tag
// records which one physics actually dispatched to; it cannot see a build that turns the thunk into a
// real adjustor jmp, only one that drops the thunk symbol entirely.
//
// Latched, not per-call: the listener sweep re-dispatches CollideWith every tick that contact persists,
// and blocking the launch is what makes the player stay in contact, so an unlatched log would fflush at
// frame rate for as long as Mina stands on the spring.
bool spring_blocked(void *contact_pair, const char *body, bool &logged)
{
    if (!collider_is_player(contact_pair) || !ability_blocked(kAbBounceSpring))
        return false;
    if (!logged)
    {
        logged = true;
        pal::logf(pal::LogLevel::Debug, "abilities: spring launch blocked (%s body)", body);
    }
    return true;
}

void repl_spring(void *self, void *contact_pair)
{
    static bool logged = false;
    if (spring_blocked(contact_pair, "exported", logged))
        return;
    if (g_orig_spring)
        g_orig_spring(self, contact_pair);
}

void repl_spring_listener(void *self, void *contact_pair)
{
    static bool logged = false;
    if (spring_blocked(contact_pair, "listener", logged))
        return;
    if (g_orig_spring_listener)
        g_orig_spring_listener(self, contact_pair);
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

// #56: no native "duck under a carryable" exists (the burrow-emerge roof/snap handles only low map
// ceilings), so with carry disabled we detect a carryable in grab range and suppress the emerge, leaving
// Mina burrowed beneath it. carryable_overhead is a read-only replica of the game's own grab query.
// Offsets from Player::PickUpAnyNearbyCarryableObject (0xd515b0, build 8de7a6b5). The two query calls go
// through the native API (mod::physics_get_aabb / mod::closest_carryable), so no AABB struct is needed
// here: the box is six floats, center.xyz then half-extent.xyz.
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
    // The game clamps the box's z half-extent to |comp+0xd8| * 1.6 * 0.3 before the query.
    const float fd8 = *reinterpret_cast<float *>(static_cast<char *>(comp) + 0xd8);
    const float clamp = (fd8 < 0.0f ? -fd8 : fd8) * 1.6f * 0.3f;
    if (clamp < box[5])
        box[5] = clamp;

    int out_n = 0;
    return mod::closest_carryable(mgr, box, layer, 1.6f, &out_n, 0ull) != nullptr; // radius 1.6, mask 0 (emerge args)
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

// TitleScreen::UpdateState(): while disconnected, keep the menu cursor off index 0 ("Start Game")
// so the wrap behaves as a two-option menu. The game has no disabled-option concept to set.
pal::TitleGateFn g_title_gate_fn;
pal::HookId g_title_gate_hook = pal::kInvalidHookId;
void (*g_orig_title_update)(void *) = nullptr;

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
void apply_title_start_text(void *self)
{
    if (self == nullptr)
        return;

    void *block = *reinterpret_cast<void **>(static_cast<char *>(self) + mth::layout::kTitleOptionBlockOff);
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

// Latches on an implausible selected index: a bad read can still look plausible on a later call, so
// the decision must not be re-derived per call once it has failed.
bool g_title_base_disabled = false;

void repl_title_update_state(void *self)
{
    int *idx = self != nullptr ? reinterpret_cast<int *>(static_cast<char *>(self) + mth::layout::kTitleSelectedIndexOff) : nullptr;
    const bool base_ok = idx != nullptr && *idx >= 0 && *idx < mth::layout::kTitleOptionCount;
    if (idx != nullptr && !base_ok && !g_title_base_disabled)
    {
        g_title_base_disabled = true;
        pal::logf(pal::LogLevel::Warn, "title: selected-index=%d out of [0,%d); cursor gate/label disabled", *idx, mth::layout::kTitleOptionCount);
    }
    const int previous = base_ok ? *idx : 0;

    if (g_orig_title_update)
        g_orig_title_update(self);

    if (!base_ok || g_title_base_disabled)
        return; // guard tripped, this call or a prior one: never write through an unverified index

    if (g_title_gate_fn && !g_title_gate_fn())
    {
        // After the game's own wrap, and direction-aware: see mth::skip_gated_option.
        *idx = mth::skip_gated_option(previous, *idx);
    }

    apply_title_start_text(self);
}

} // namespace

namespace pal
{

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
        logf(LogLevel::Warn, "modifiers: symbols unresolved (mgr=0x%llx slot=0x%llx toggle=0x%llx set=0x%llx); feature disabled",
             static_cast<unsigned long long>(g_mod_save_manager), static_cast<unsigned long long>(g_addr_activate_slot),
             static_cast<unsigned long long>(g_addr_toggle), static_cast<unsigned long long>(g_addr_set_applied));
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
    // The native entry covers the live slot only (it resolves *(g_saveManager+0x18) itself) and does
    // NOT null-check it, so it is only safe once that slot is known good; the apply-path slot has no
    // native path either way and keeps the raw write.
    const bool native = pal::pointer_looks_valid(lslot) && mod::cheat_manager_set_cheat_applied(idx, on);
    if (!native)
        set_mask_bit(lslot, idx, on);
    if (aslot != lslot)
        set_mask_bit(aslot, idx, on);
    if (!pal::pointer_looks_valid(aslot) || !pal::pointer_looks_valid(lslot))
        logf(LogLevel::Warn, "modifiers: live set idx=%d partial (apply=%p live=%p)", idx, aslot, lslot);
    // ActivateSaveCheats reads the apply-path slot internally, so only rebuild when it is valid.
    if (!pal::pointer_looks_valid(aslot) || !mod::cheat_manager_activate_save_cheats())
        logf(LogLevel::Warn, "modifiers: live set idx=%d bit written but mirror NOT rebuilt (apply slot/native entry unavailable)", idx);
    logf(LogLevel::Info, "modifiers: live set idx=%d on=%d apply=%p live=%p", idx, static_cast<int>(on), aslot, lslot);
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

bool runtime_modifiers_available()
{
    ensure_cheat_mgr_symbol();
    return g_cheat_mgr_sym != nullptr;
}

bool runtime_modifier_ready()
{
    const CheatManagerView mgr = cheat_manager();
    return mgr.base != nullptr && mgr.base[kCheatMgrActivatedOff] != 0;
}

bool set_runtime_modifier(int idx, bool on)
{
    if (idx < 0 || idx >= 254)
        return false;
    const CheatManagerView mgr = cheat_manager();
    if (mgr.base == nullptr)
        return false;
    // Every read site checks this byte before the mask, so a bit written while it is clear does
    // nothing at all. Only the game's own ActivateSaveCheats sets it, so a clear byte means no save
    // is active yet, and the write is worth attempting again only after the next activation.
    if (mgr.base[kCheatMgrActivatedOff] == 0)
        return false;
    mth::cheat_mask_set(mgr.mask, idx, on);
    return true;
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
    g_up_ok = g_up_save_manager != 0;
    if (!g_up_ok)
        logf(LogLevel::Warn, "upgrades: g_saveManager unresolved; feature disabled");
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
    // Checked before any write: the owned-bit fields do nothing until UpdateStats recomputes the maxima
    // from them, so without it the grant would be half-applied instead of merely deferred.
    if (!mod::player_stats_api_available())
    {
        trace(6, "skip: PlayerUpdateStats unavailable", nullptr);
        return false;
    }
    void *slot = active_save_slot(g_up_save_manager);
    if (slot == nullptr)
    {
        trace(4, "skip: active SaveSlot* null", slot);
        return false;
    }
    // CombatCore is reached through the Player, so a non-canonical read means the Player is not one (#157
    // faulted on exactly this load, walking a freed Player). Bail rather than treat it as absent: UpdateStats
    // and the magic-pool writes below still go through that same pointer. Leaves the counts dirty, so the
    // next tick retries. A genuinely null CombatCore is the separate case the pool restores already handle.
    void *cc = *reinterpret_cast<void **>(static_cast<char *>(player) + kCombatCoreOff);
    if (cc != nullptr && !pal::pointer_looks_valid(cc))
    {
        trace(5, "skip: CombatCore* invalid", slot);
        return false;
    }

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
    mod::player_update_stats(); // recompute live maxima from the owned-bit fields; acts on the live player

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
    g_addr_rope_climb = resolve_game_symbol(mth::sym::player_rope_climb_start);
    g_addr_bounce_plant = resolve_game_symbol(mth::sym::bounce_plant_collide);
    g_addr_bounce_launch = resolve_game_symbol(mth::sym::bounce_plant_launch);
    g_addr_on_bounce = resolve_game_symbol(mth::sym::player_on_bounce);
    g_addr_spring = resolve_game_symbol(mth::sym::spring_bellows_collide);
    g_addr_spring_listener = resolve_game_symbol(mth::sym::spring_bellows_collide_listener);
    g_addr_pickup = resolve_game_symbol(mth::sym::player_pickup_carryable);
    g_addr_train_npc = resolve_game_symbol(mth::sym::train_authority_on_npc_event);
    g_addr_burrow_jump = resolve_game_symbol(mth::sym::mina_on_burrow_jump); // #56
    g_ab_ok = g_addr_burrow_ground != 0 || g_addr_rope_climb != 0 || g_addr_bounce_plant != 0 || g_addr_bounce_launch != 0 || g_addr_on_bounce != 0 ||
              g_addr_spring != 0 || g_addr_spring_listener != 0 || g_addr_pickup != 0 || g_addr_train_npc != 0;
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

    // The copy physics actually dispatches to; the exported one above is unreachable here (#188).
    if (g_addr_spring_listener != 0)
    {
        g_id_spring_listener = hook_engine().install_hook(reinterpret_cast<void *>(g_addr_spring_listener), reinterpret_cast<void *>(&repl_spring_listener),
                                                          reinterpret_cast<void **>(&g_orig_spring_listener));
        if (g_id_spring_listener == kInvalidHookId)
            logf(LogLevel::Error, "abilities: failed to hook SpringBellows::CollideWith (listener body)");
    }
    else
        logf(LogLevel::Warn, "abilities: SpringBellows::CollideWith listener body not resolved; springs launch ungated");

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
                     g_id_on_bounce != kInvalidHookId || g_id_spring != kInvalidHookId || g_id_spring_listener != kInvalidHookId ||
                     g_id_carry != kInvalidHookId || g_id_train != kInvalidHookId || g_id_burrow_jump != kInvalidHookId;
    if (any)
        logf(LogLevel::Info, "abilities: ability gating hooks installed");
    else
        g_ability_block = nullptr;
    return any;
}

void remove_ability_hooks()
{
    for (HookId *id : {&g_id_burrow, &g_id_rope, &g_id_puff, &g_id_launch, &g_id_on_bounce, &g_id_spring, &g_id_spring_listener, &g_id_carry, &g_id_train,
                       &g_id_burrow_jump})
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
