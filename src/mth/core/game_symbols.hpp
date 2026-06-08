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

// s_rItems: 195-entry item table (stride 0x68, kind at +0x28).
inline constexpr const char *s_r_items = "_ZN12_GLOBAL__N_18s_rItemsE";

inline constexpr const char *pickup_init = "_ZN6Pickup4InitEii";                         // Pickup::Init(int itemType, int locIdx)
inline constexpr const char *pickup_on_pickup = "_ZN6Pickup8OnPickupEP14PickupListener"; // Pickup::OnPickup(PickupListener*)

// ShopMenu::ItemPresent(): shop-buy grant funnel; calls Items::OnPickup directly (no Pickup entity).
// Menu stashes locIdx at ShopMenu+0x218 and itemType at +0x21c before this fires.
inline constexpr const char *shop_item_present = "_ZN8ShopMenu11ItemPresentEv"; // ShopMenu::ItemPresent()

// ycWorld::QueueDestroy: unconditional teardown (no SpawnPoint gate); writes no save/grant state.
inline constexpr const char *queue_destroy = "_ZN7ycWorld12QueueDestroyEP8ycEntityb"; // ycWorld::QueueDestroy(ycEntity*, bool)

// SetItemCollected: side-effect-free bitfield write for keys/bonestones/fish; feeds native reload gate.
// s_rItemCollection: 361 x 0x50, native itemType at +0x18, maps locIdx to vanilla contents kind.
inline constexpr const char *set_item_collected =
    "_ZN5Items16SetItemCollectedEibP14ItemCollectionP8SaveSlot";                            // Items::SetItemCollected(int, bool, ItemCollection*, SaveSlot*)
inline constexpr const char *s_r_item_collection = "_ZN12_GLOBAL__N_117s_rItemCollectionE"; // s_rItemCollection location table

} // namespace mth::sym
