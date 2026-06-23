#pragma once

namespace mth::sym
{

// Mangled symbol names. Stable across recompiles; verified against the unstripped Linux binary.
inline constexpr const char *game_fixed_update = "_ZN4Game11FixedUpdateEv";                      // Game::FixedUpdate()
inline constexpr const char *game_update = "_ZN4Game6UpdateEf";                                  // Game::Update(float)
inline constexpr const char *world_update = "_ZN5World6UpdateEP20ycUpdateQueueContext";          // World::Update(ycUpdateQueueContext*)
inline constexpr const char *update_queue = "_ZN13ycUpdateQueue6UpdateEf";                       // ycUpdateQueue::Update(float)
inline constexpr const char *on_pickup_done = "_ZN5Items12OnPickupDoneEiiP6PlayerRK6ycVec3iijb"; // Items::OnPickupDone(...)
inline constexpr const char *process_sdl_event = "_Z15ProcessSDLEventR9SDL_Event";               // ProcessSDLEvent(SDL_Event&)

// Inbound grant plumbing: Player* via ctor, trackable via Update each frame, position via GetPos.
inline constexpr const char *player_ctor =
    "_ZN6PlayerC2EP8ycEntityP17GameComponentDescP11PlayerSetup"; // Player::Player(ycEntity*, GameComponentDesc*, PlayerSetup*)
inline constexpr const char *player_trackable_update = "_ZN15PlayerTrackable6UpdateEP20ycUpdateQueueContext"; // PlayerTrackable::Update(ycUpdateQueueContext*)
inline constexpr const char *player_trackable_get_pos = "_ZNK15PlayerTrackable6GetPosEv";                     // PlayerTrackable::GetPos() const -> ycVec3
inline constexpr const char *player_update_stats =
    "_ZN6Player11UpdateStatsEv"; // Player::UpdateStats() -> recompute max HP/magic/spark/vial/trinket from save bits

// s_rItems: 195-entry item table (stride 0x68, kind at +0x28).
inline constexpr const char *s_r_items = "_ZN12_GLOBAL__N_18s_rItemsE";

// RoomManager::Update: per-frame tick on the room-transition state machine. self == RoomManager instance;
// the live current-room index is read off it (Linux +0x1b4 / Windows +0x1bc) by pal::current_room_index.
inline constexpr const char *room_manager_update = "_ZN11RoomManager6UpdateEP20ycUpdateQueueContext";

// AreaManager::NewArea(int prevIdx, int newIdx): fires on area change. newIdx is the dense area index
// (0..197); captured to qualify the per-area room index into a globally-unique screen id.
inline constexpr const char *area_new_area = "_ZN11AreaManager7NewAreaEii";

inline constexpr const char *pickup_init = "_ZN6Pickup4InitEii";                         // Pickup::Init(int itemType, int locIdx)
inline constexpr const char *pickup_on_pickup = "_ZN6Pickup8OnPickupEP14PickupListener"; // Pickup::OnPickup(PickupListener*)

// ShopMenu::ItemPresent(): shop-buy grant funnel; calls Items::OnPickup directly (no Pickup entity).
// Menu stashes locIdx at ShopMenu+0x218 and itemType at +0x21c before this fires.
inline constexpr const char *shop_item_present = "_ZN8ShopMenu11ItemPresentEv"; // ShopMenu::ItemPresent()
// Windows: ShopMenu::ItemPresent is inlined into ShopMenu::InitState; hook InitState there.
inline constexpr const char *shop_init_state = "_ZN8ShopMenu9InitStateEv"; // ShopMenu::InitState()

// ycWorld::QueueDestroy: unconditional teardown (no SpawnPoint gate); writes no save/grant state.
inline constexpr const char *queue_destroy = "_ZN7ycWorld12QueueDestroyEP8ycEntityb"; // ycWorld::QueueDestroy(ycEntity*, bool)

// SetItemCollected: side-effect-free bitfield write for keys/bonestones/fish; feeds native reload gate.
// s_rItemCollection: 361 x 0x50, native itemType at +0x18, maps locIdx to vanilla contents kind.
inline constexpr const char *set_item_collected =
    "_ZN5Items16SetItemCollectedEibP14ItemCollectionP8SaveSlot";                            // Items::SetItemCollected(int, bool, ItemCollection*, SaveSlot*)
inline constexpr const char *s_r_item_collection = "_ZN12_GLOBAL__N_117s_rItemCollectionE"; // s_rItemCollection location table

// Live boss-death funnels (kill-time). SetBossDefeated was reload-path only (29/34 call sites are in
// <Boss>::InitState corpse-spawn). Most bosses route through TriggerDeathSequence (its 1-arg variant
// tail-jumps into this 2-arg one); Lionel/Maxi route through OnDefeatedNoSkeleton. Boss index is at
// bossComponent+0x68. Some bosses hit both funnels in one death -> dedup at the bridge.
inline constexpr const char *boss_trigger_death_sequence =
    "_ZN13BossComponent20TriggerDeathSequenceEP15BossDeathParamsj"; // BossComponent::TriggerDeathSequence(BossDeathParams*, unsigned)
inline constexpr const char *boss_on_defeated_no_skeleton =
    "_ZN13BossComponent20OnDefeatedNoSkeletonER19BossDeathRewardInfo"; // BossComponent::OnDefeatedNoSkeleton(BossDeathRewardInfo&)

// KeyBlock: the kear-lock entity. Slot resolved at Update time via name-scan (KeyBlock+0x2d0 is -1 for non-PairLock).
inline constexpr const char *key_block_update = "_ZN8KeyBlock6UpdateEP20ycUpdateQueueContext"; // KeyBlock::Update(ycUpdateQueueContext*)
// Multi-block lock (KeyBlockChain) + kear-locked Chest, opened/unlocked live by a per-frame hook.
// Windows hooks UpdateState because the game's MSVC linker ICF-folds the byte-identical thin Update
// wrappers across classes (per-class Update is not a distinct function); UpdateState runs on the
// StateMachine sub-object at base+0x170, so the PAL recovers base (install_chain_open_hook/...).
inline constexpr const char *key_block_chain_update = "_ZN13KeyBlockChain6UpdateEP20ycUpdateQueueContext"; // [Linux]
inline constexpr const char *chest_update = "_ZN5Chest6UpdateEP20ycUpdateQueueContext";                    // [Linux]
inline constexpr const char *key_block_chain_update_state = "_ZN13KeyBlockChain11UpdateStateEv";           // [Windows]
inline constexpr const char *chest_update_state = "_ZN5Chest11UpdateStateEv";                              // [Windows]
// Active SaveSlot* = *(g_saveManager+0x18); lock-unlocked bits live in a u64 at SaveSlot+0x200.
inline constexpr const char *save_manager = "g_saveManager";

// SaveSlot::Clear(bool): new-file reset; writes the default starting-upgrade fields (region-18 kit).
// Called only at new-file creation, so a post-hook field zero never touches a progressed save.
inline constexpr const char *save_slot_clear = "_ZN8SaveSlot5ClearEb"; // SaveSlot::Clear(bool)

// Deathlink. InitDeath = deepest once-per-death convergence (DETECT, edge via Player+0x1380);
// TriggerDeath = APPLY (call on the live Player to kill). Player+0x1380 = once-per-death guard byte.
inline constexpr const char *player_init_death = "_ZN6Player9InitDeathEb";        // Player::InitDeath(bool)
inline constexpr const char *player_trigger_death = "_ZN6Player12TriggerDeathEv"; // Player::TriggerDeath()

// Modifiers ("cheats"). Apply hub reads SaveSlot at *(g_saveManager+0x08); live gameplay uses
// *(g_saveManager+0x18). ToggleCheat (menu) + SetCheatApplied (cheat-code) are the only two
// player write paths; ActivateSaveCheats rebuilds the runtime mirror at [CheatManager+0x20].
inline constexpr const char *activate_save_slot = "_ZN14SessionManager16ActivateSaveSlotEb";        // SessionManager::ActivateSaveSlot(bool)
inline constexpr const char *activate_save_cheats = "_ZN12CheatManager18ActivateSaveCheatsEv";      // CheatManager::ActivateSaveCheats()
inline constexpr const char *toggle_cheat = "_ZN12CheatManager11ToggleCheatEibP8SaveSlotbi";        // CheatManager::ToggleCheat(int,bool,SaveSlot*,bool,int)
inline constexpr const char *set_cheat_applied = "_ZN12CheatManager15SetCheatAppliedEibP8SaveSlot"; // CheatManager::SetCheatApplied(int,bool,SaveSlot*)

// Per-stat level cap. Linux detours GetNewGameMaxLevelPlayer (the buy-gate's only live caller) to return
// the per-stat cap, with UpdateState wrapped to supply the cursor stat. Windows inlines the cap and the
// standalone UpdateState is dead code, so it hooks the per-frame LevelUpMenu::Update and presents capped
// stats as already-maxed so the inlined cap gate trips.
inline constexpr const char *level_up_menu_update_state = "_ZN11LevelUpMenu11UpdateStateEv"; // LevelUpMenu::UpdateState() [Linux]
inline constexpr const char *get_new_game_max_level_player =
    "_ZN10CombatData24GetNewGameMaxLevelPlayerEiiP8SaveSlot";                                          // CombatData::GetNewGameMaxLevelPlayer [Linux]
inline constexpr const char *level_up_menu_update = "_ZN11LevelUpMenu6UpdateEP20ycUpdateQueueContext"; // LevelUpMenu::Update(...) [Windows]

} // namespace mth::sym
