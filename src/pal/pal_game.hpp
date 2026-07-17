#pragma once

#include <cstdint>
#include <functional>

namespace pal
{

// World-space player position from a PlayerTrackable*. false if unavailable.
// Windows reads base fields directly; calling the real GetPos faults off its own call sites.
bool read_player_position(const void *trackable, float out[3]);

// Live current-room index off the RoomManager instance. false if null or negative (no room yet).
// Field offset is per-platform/per-build (Linux 0x1b4, Windows 0x1bc).
bool current_room_index(void *room_manager, std::uint32_t *out);

// Hook the new-game edge (SaveSlot::InitGamestate, plus SaveSlot::Clear to discount the platform
// re-entry; see the impl). When should_suppress() returns true, zero the new game's default upgrade
// fields so AP supplies the starting inventory instead. false if unresolved.
using NewfileKitSuppressFn = std::function<bool()>;
bool install_newfile_kit_suppressor(NewfileKitSuppressFn should_suppress);
void remove_newfile_kit_suppressor();

// Fires on the game thread when a NEW GAME (or NG+) starts, with the SaveSlot* the new game is being
// created in. The caller binds that slot rather than guessing from whatever save is live, which is not
// the new one yet at this point. Shares the hooks installed by install_newfile_kit_suppressor, so it is
// inert unless those resolved. Callback runs mid-menu-transition: keep it minimal (set a flag).
using NewGameFn = std::function<void(void *save_slot)>;
void set_new_game_hook(NewGameFn on_new_game);
void remove_new_game_hook();

// Pickup* base from the `this` passed to a Pickup::OnPickup hook.
// MSVC: that `this` is the PickupListener MI subobject, not the Pickup base.
void *pickup_base_from_onpickup(void *onpickup_this);

// Active SaveSlot* from the resolved g_saveManager global. Linux derefs +0x18; on Windows the
// global already holds the SaveSlot* directly. Returns nullptr if the global is 0.
void *active_save_slot(std::uintptr_t save_manager_global);

// Live 0-based index (0/1/2) of the currently-active save slot, read from the save-manager global.
// Returns -1 when no save is active. Field offset is per-platform (Linux +0x20, Windows +0x8).
[[nodiscard]] int live_save_slot_index(std::uintptr_t save_manager_global);

// Shop-purchase callback: given the bought slot, runs the AP collect logic and returns the itemType
// the game should store (a dummy to suppress the vanilla grant where the platform redirects, else
// the original itemType unchanged).
using ShopBuyFn = int (*)(int loc_idx, int item_type);

// Install the shop-purchase detection hook. The platform owns which game function it hooks
// (ShopMenu::ItemPresent on Linux, ShopMenu::InitState on Windows), the field layout, and the
// commit detection; on a committed buy it invokes on_buy. Returns false if not installed.
bool install_shop_purchase_hook(ShopBuyFn on_buy);
void remove_shop_purchase_hook();

// Per-level shop sold-out / level-advance override. The vanilla grant that advances a slot and drops
// its stock is suppressed for AP shop slots, so without this the shop refills every reopen (issue #48)
// and tiered slots (same slot, rising price per level) never advance. The platform owns the hook
// (ShopItem::Refresh) and walks the slot's level chain; level_state(loc_idx) classifies each level's
// AP location: 0 = not an AP location, 1 = AP location not yet checked, 2 = AP location already
// checked. Returns false if not installed.
using ShopLevelFn = int (*)(int loc_idx);
bool install_shop_stock_hook(ShopLevelFn level_state);
void remove_shop_stock_hook();

// Shop flattening: while active, OR the game's never-stack bit onto each ShopDef before OpenShop
// builds its box list, so stacked slots show one buyable box per level. `active` is polled per
// Shop::Get call (game thread); the bit is only ever set, never cleared, so a disconnect leaves
// already-touched shops flat until the game restarts.
using ShopFlattenFn = bool (*)();
bool install_shop_flatten_hook(ShopFlattenFn active);
void remove_shop_flatten_hook();

// Shop item text: post-hook ShopMenu::SetCursor; the callback receives the ShopMenu and uses the
// helpers below to read the selected location / enumerate boxes / rewrite the name+desc widgets.
using ShopTextFn = void (*)(void *shop_menu);
bool install_shop_text_hook(ShopTextFn on_set_cursor);
void remove_shop_text_hook();

// ShopMenu accessors + text mutators (offsets live in the PAL, not in src/mth/).
[[nodiscard]] int shop_selected_loc(void *shop_menu); // -1 if sold-out / itemType 0x65 / invalid
void shop_enumerate_locs(void *shop_menu, void (*sink)(int loc, void *ctx), void *ctx);
[[nodiscard]] void *shop_name_widget(void *shop_menu);
[[nodiscard]] void *shop_desc_widget(void *shop_menu);
void shop_set_text(void *widget, const char *utf8);
void shop_set_color(void *widget, std::uint32_t rgba);

// Per-frame "open a removed lock" hooks for KeyBlockChain / locked Chest. The platform owns the hook
// target and the this->base normalization: Linux hooks ::Update (self == entity base); Windows hooks
// ::UpdateState (self == the StateMachine sub-object, so base = self - 0x170) because the game's MSVC
// linker ICF-folds the per-class ::Update wrappers. on_frame runs each frame with the entity base.
using EntityFrameFn = void (*)(void *base);
bool install_chain_open_hook(EntityFrameFn on_frame);
void remove_chain_open_hook();
bool install_chest_unlock_hook(EntityFrameFn on_frame);
void remove_chest_unlock_hook();

// ---- Modifier ("cheat") control. All offset/symbol/game-call divergence lives in the PAL impl. ----

// True once every modifier symbol (ActivateSaveSlot, ActivateSaveCheats, ToggleCheat,
// SetCheatApplied, g_saveManager) resolves. The feature no-ops if false. Resolves + caches on
// first call.
bool modifiers_available();

// Install the ActivateSaveSlot prologue hook (and an ActivateSaveCheats capture hook). `seed` is
// invoked on the game thread before the original, with the active 0-based save-slot index and the
// slot's 8 cheat-mask words (in/out); it mutates only the words it wants. No-op if modifiers
// unavailable.
using SeedFn = std::function<void(int slot_index, std::uint32_t *words /*[8]*/)>;
void set_new_game_modifier_seed(SeedFn seed);

// Install the ToggleCheat + SetCheatApplied lockdown hooks. `block(idx)` returns true to suppress
// that player change (early-return). No-op if modifiers unavailable.
using BlockFn = std::function<bool(int idx)>;
void set_modifier_lockdown(BlockFn block);

// Set/clear a modifier's enable bit on the live slot(s) and rebuild the runtime mirror so the
// effect is live. Game-thread only (calls ActivateSaveCheats). Returns false if unavailable or idx
// invalid. Writes both the apply-path and live slots to sidestep the unresolved aliasing.
bool apply_live_modifier(int idx, bool on);

// Remove the modifier hooks and clear the callbacks. Called by the mth/ owner's destructor.
void remove_modifier_hooks();

// ---- Per-stat level cap. All symbol/offset/game-call divergence lives in the PAL impl. ----

// True once LevelUpMenu::UpdateState and CombatData::GetNewGameMaxLevelPlayer both resolve.
// Resolves + caches on first call. The feature no-ops if false.
bool level_cap_available();

// Install the level-cap hooks. `cap(stat, vanilla_cap)` runs on the game thread during the
// LevelUpMenu buy-gate and returns the cap to enforce for `stat` (0=attack,1=defense,2=sidearm);
// return vanilla_cap for "no restriction". No-op if unavailable.
using LevelCapFn = std::function<int(int stat, int vanilla_cap)>;
void set_level_cap_provider(LevelCapFn cap);

// Remove the level-cap hooks and clear the callback. Called by the mth/ owner's destructor.
void remove_level_cap_hook();

// ---- Capacity upgrades (itemTypes 68..72). Symbol/offset divergence lives in the PAL impl. ----

// True once the active SaveSlot global and Player::UpdateStats resolve. Resolves + caches on first
// call; the feature no-ops if false.
bool upgrades_available();

// Apply per-type upgrade counts (index order Magic,Health,Spark,Vial,Trinket; already clamped to
// caps) to the active save: set that many low bits in each per-type field, then call
// Player::UpdateStats(player) to recompute live maxima. `counts` has mth::kUpgradeCount entries.
// Game-thread only. false if unavailable or player is null (caller retries).
bool apply_upgrades(const int *counts, void *player);

// ---- Ability gating. Symbol/offset/game-call divergence lives in the PAL impl. ----

// True once at least one ability chokepoint symbol resolves on this platform.
bool abilities_available();

// Gate predicate consulted by the detours on the game thread. `a` is the mth::Ability ordinal
// (int, to keep pal/ free of mth/ headers).
using AbilityBlockFn = std::function<bool(int ability_ordinal)>;

// Install the ability detours; each suppresses its action when block(ordinal) is true. No-op for
// unresolved symbols. Returns false if none installed.
bool install_ability_hooks(AbilityBlockFn block);
void remove_ability_hooks();

// Forces the train-present save byte to 0 while blocked; the arrival event re-sets it when
// unblocked. No-op if unavailable. Game-thread only.
void enforce_train_presence(std::uintptr_t save_manager_global, bool blocked);

// Clamps the SaveSlot unlocked-train-lines bitfield to line_mask (bit N = destination line N travelable).
// The game auto-unlocks a line just by visiting its station (#98), so AP-only gating overwrites the field
// with the granted-ticket mask each frame. This is box-hiding UX only: the menu builder always shows lines
// 95/99 regardless, so the warp is refused by the destination gate below. No-op if unavailable. Game-thread.
void enforce_train_destinations(std::uintptr_t save_manager_global, std::uint32_t line_mask);

// Requires the generic Train Pass (item 94) before the train can be boarded: forces the train-present byte
// to 0 while the pass is unowned, then releases it once received (#98). No-op if unavailable. Game-thread.
void enforce_train_boarding(std::uintptr_t save_manager_global);

// Publishes the train_rando destination gate read by the OnNPCEvent detour. When rando_active, a selected
// ticket line is cancelled unless its bit is in granted_mask; when inactive the detour uses the console
// Train-ability block instead. Cheap; call each tick.
void set_train_destination_gate(std::uint32_t granted_mask, bool rando_active);

// ---- Pawn shop ("Pawnty") disable. Symbol/offset divergence lives in the PAL impl. ----

// PawnShopNPC::OnNPCEvent suppressor. When disable() returns true the detour no-ops every event and
// vetoes the interactable query (no prompt); otherwise it calls through. disable() runs on the game
// thread, so it must be cheap and thread-safe. Returns false if the chokepoint did not resolve.
using PawnShopBlockFn = std::function<bool()>;
bool install_pawn_shop_hook(PawnShopBlockFn disable);
void remove_pawn_shop_hook();

// ---- Ossex fountain lamp pre-light. Symbol/offset divergence lives in the PAL impl. ----

// Returns the current "force lit" lamp bitmask (bit i => force lamp index i lit). Called per bulb
// per frame from the HubFountain::Bulb::Update detour.
using FountainLampFn = std::function<std::uint32_t()>;
bool install_fountain_lamp_hook(FountainLampFn lit_mask);
void remove_fountain_lamp_hook();

// ---- AP save-slot gate. Published each tick by App; guards independent game-thread detours. ----

// Published each tick by App: true when the loaded save is the AP (seed, slot)'s bound save.
// The independent game-thread detours (collected-bit, kear, pickup-check, deathlink) read this
// before mutating the save or sending to the server. Defaults to false (closed) until set.
void set_ap_save_gate(bool open);
[[nodiscard]] bool ap_save_gate();

} // namespace pal
