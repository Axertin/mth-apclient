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

// Pickup* base from the `this` passed to a Pickup::OnPickup hook.
// MSVC: that `this` is the PickupListener MI subobject, not the Pickup base.
void *pickup_base_from_onpickup(void *onpickup_this);

// Active SaveSlot* from the resolved g_saveManager global. Linux derefs +0x18; on Windows the
// global already holds the SaveSlot* directly. Returns nullptr if the global is 0.
void *active_save_slot(std::uintptr_t save_manager_global);

// Shop-purchase callback: given the bought slot, runs the AP collect logic and returns the itemType
// the game should store (a dummy to suppress the vanilla grant where the platform redirects, else
// the original itemType unchanged).
using ShopBuyFn = int (*)(int loc_idx, int item_type);

// Install the shop-purchase detection hook. The platform owns which game function it hooks
// (ShopMenu::ItemPresent on Linux, ShopMenu::InitState on Windows), the field layout, and the
// commit detection; on a committed buy it invokes on_buy. Returns false if not installed.
bool install_shop_purchase_hook(ShopBuyFn on_buy);
void remove_shop_purchase_hook();

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

} // namespace pal
