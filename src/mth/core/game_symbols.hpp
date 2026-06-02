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

} // namespace mth::sym
