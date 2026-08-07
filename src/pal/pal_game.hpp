#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>

namespace pal
{

// Live current-room index off the RoomManager instance. false if null or negative (no room yet).
// Field offset is per-platform/per-build (Linux 0x1b4, Windows 0x1bc).
bool current_room_index(void *room_manager, std::uint32_t *out);

// Hook SaveSlot::Clear (called only at new-file creation); when should_suppress() returns true, zero the
// default region-18 upgrade fields so AP supplies the starting inventory instead. false if unresolved.
using NewfileKitSuppressFn = std::function<bool()>;
bool install_newfile_kit_suppressor(NewfileKitSuppressFn should_suppress);
void remove_newfile_kit_suppressor();

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

// Raises the station donation machine's progress counter to `seed`, never lowering it. The machine's
// completion goal is compiled in, so pre-paying part of it is what makes the donation cost less; the game
// also clamps how much the player can dial in to what is still owed, so this bounds the charge too (#162).
// seed 0 leaves the counter alone (vanilla cost). No-op if unavailable. Game-thread.
void seed_ticket_machine_progress(std::uintptr_t save_manager_global, std::uint32_t seed);

// Publishes the train_rando destination gate read by the OnNPCEvent detour. When rando_active, a selected
// ticket line is cancelled unless its bit is in granted_mask; when inactive the detour uses the console
// Train-ability block instead. Cheap; call each tick.
void set_train_destination_gate(std::uint32_t granted_mask, bool rando_active);

// ---- Burrow/swim boundary (#163). ----

// Observers for the burrow lifetime, invoked on the game thread by the burrow detours: on_commit(deep) when
// a burrow/swim commit is allowed through (deep = it classified as a swim), on_emerge() when the burrow
// actually ends. Together they track which mode the player is in WITHOUT reading the burrow-mode field,
// whose offset drifts between builds. Cleared by remove_ability_hooks().
using BurrowCommitFn = std::function<void(bool deep)>;
using BurrowEmergeFn = std::function<void()>;
void set_burrow_observers(BurrowCommitFn on_commit, BurrowEmergeFn on_emerge);

// Live swim-vs-land reading for `player`, the same discriminator the commit classifier uses: -1 unknown
// (null player, or it did not resolve), 0 shallow, 1 deep. Game-thread only.
int burrow_water_state(void *player);

// True only while the player is actually in the burrow state. The commit/emerge observers are not enough on
// their own: a burrow also ends via damage, get-hit, death, the pit check and the land/idle transitions, none
// of which reach the emerge commit. Callers confirm the arm with this every tick, because forcing an emerge on
// a player who is NOT burrowed is destructive (it runs a carryable pickup, rewrites facing, and can teleport
// them). Reads the state field, not the burrow-medium field whose offset drifts. Game-thread only.
bool player_is_burrowing(void *player);

// Forces the burrow-emerge commit, surfacing the player. false if unavailable. Game-thread only.
bool force_burrow_emerge(void *player);

// Hands the player to the game's OWN on-foot deep-water fall by requesting that player state: the game then
// drives the whole recovery itself (fall animation and sound, respawn at the shore, pit damage, and whether
// that damage is fatal). Requesting the state is the only sound way in, since the teleport and the damage
// live in later states driven by the state machine's own timer. The drift guard is the caller's
// player_is_burrowing() check; the range check here only covers a caller that skipped it. Game-thread only.
bool request_deep_water_fall(void *player);

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

// ---- Title-screen gating. Offsets are shared (see mth::layout); only symbol resolution diverges. ----

// Returns true when AP is connected, i.e. when "Start Game" is selectable. Called once per
// TitleScreen::UpdateState.
using TitleGateFn = std::function<bool()>;
bool install_title_gate_hook(TitleGateFn connected);
void remove_title_gate_hook();

// Backstop for the cursor gate above: TitleScreen::UpdateState performs the confirm dispatch in the
// same call it writes the cursor, so a cursor correction can lose the race with StartGame already
// having run. Suppresses the vanilla StartGame when the callback returns true.
using StartGameSuppressFn = std::function<bool()>;
bool install_start_game_suppress_hook(StartGameSuppressFn suppress);
void remove_start_game_suppress_hook();

// Sets the "Start Game" option's label. Applied from the UpdateState detour rather than once at
// init, because the game's localization refresh unconditionally re-sets all three option strings.
// An empty/null text restores the original (cached on first sight, never an English literal).
void set_title_start_option_text(const char *text);

// ---- Save takeover. Symbol/offset divergence lives in the PAL impl. ----

// SaveManager's "persist a slot" chokepoint: observed, never suppressed. Fires on the game's own
// save cadence, which is the flush trigger for mod-owned saves.
using SaveRequestedFn = std::function<void()>;
bool install_save_request_hook(SaveRequestedFn on_save);
void remove_save_request_hook();

// Fires at the top of ProfileSelectMenu::UpdateState with the live menu, every frame it updates.
// The pointer is normalized to the primary `this` (Windows detours receive a base subobject), and
// the menu only exists while the game is in profile-select, so callbacks must not cache it.
using ProfileMenuFn = std::function<void(void *menu)>;
[[nodiscard]] bool install_profile_menu_hook(ProfileMenuFn on_update);
void remove_profile_menu_hook();

// Runs the game's own new-file init on the vanilla SaveSlot ARRAY element for `slot`, not the live
// working slot: the launch copies array over working, so a working-slot write here is discarded.
// Clear(false) then InitGamestate(); true would be the NG+ cycle. Deliberately does not persist;
// WriteSaveData is not on the launch path. False if the symbols or the slot address do not resolve.
bool init_new_save_file(unsigned int slot);

// The mod's own save directory: a "saves" subdirectory of the same base pal::log_dir() resolves to.
std::filesystem::path mod_save_dir();

} // namespace pal
