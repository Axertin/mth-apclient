#pragma once

namespace mth::sym
{

// Mangled names of the game functions we hook. STABLE across recompiles (only the
// addresses move), so these are not keyed by build. Verified against the unstripped
// Linux binary (`nm MinaTheHollower`).
inline constexpr const char *game_fixed_update = "_ZN4Game11FixedUpdateEv";                      // Game::FixedUpdate()
inline constexpr const char *game_update = "_ZN4Game6UpdateEf";                                  // Game::Update(float)
inline constexpr const char *world_update = "_ZN5World6UpdateEP20ycUpdateQueueContext";          // World::Update(ycUpdateQueueContext*)
inline constexpr const char *update_queue = "_ZN13ycUpdateQueue6UpdateEf";                       // ycUpdateQueue::Update(float)
inline constexpr const char *on_pickup_done = "_ZN5Items12OnPickupDoneEiiP6PlayerRK6ycVec3iijb"; // Items::OnPickupDone(...)
inline constexpr const char *process_sdl_event = "_Z15ProcessSDLEventR9SDL_Event";               // ProcessSDLEvent(SDL_Event&)

// For replaying inbound item grants we need a live Player* and the player's current world
// position. Both come from hooks/calls (no hardcoded struct offsets): the Player ctor gives
// the Player*; PlayerTrackable::Update (runs every frame) gives the trackable instance; and
// PlayerTrackable::GetPos() is the engine's canonical live-position accessor (camera-tracked).
inline constexpr const char *player_ctor =
    "_ZN6PlayerC2EP8ycEntityP17GameComponentDescP11PlayerSetup"; // Player::Player(ycEntity*, GameComponentDesc*, PlayerSetup*)
inline constexpr const char *player_trackable_update = "_ZN15PlayerTrackable6UpdateEP20ycUpdateQueueContext"; // PlayerTrackable::Update(ycUpdateQueueContext*)
inline constexpr const char *player_trackable_get_pos = "_ZNK15PlayerTrackable6GetPosEv";                     // PlayerTrackable::GetPos() const -> ycVec3

// (anonymous namespace)::s_rItems - the 195-entry item table (stride 0x68); the storage
// kind is the int at +0x28. Used only to log each grant's item kind (diagnostic).
inline constexpr const char *s_r_items = "_ZN12_GLOBAL__N_18s_rItemsE";

// Outbound (functional dummy). Pickup::Init is the single spawn convergence point (re-derives
// itemType for locIdx>=0, stores it on the entity); Pickup::OnPickup is the collect chokepoint.
inline constexpr const char *pickup_init = "_ZN6Pickup4InitEii";                         // Pickup::Init(int itemType, int locIdx)
inline constexpr const char *pickup_on_pickup = "_ZN6Pickup8OnPickupEP14PickupListener"; // Pickup::OnPickup(PickupListener*)

} // namespace mth::sym
