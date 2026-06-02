#pragma once

#include <cstdint>

#include "mth/core/build_id.hpp"

namespace mth
{

// Per-build addresses of the engine tick functions, as load-base-relative
// offsets (the runtime address is pal::game_module().base + offset). These are
// read off the analyzed binary; an all-zero table means "this build is not
// mapped yet" and GameHooks installs nothing.
struct GameOffsets
{
    std::uintptr_t game_fixed_update; // Game::FixedUpdate()
    std::uintptr_t game_update;       // Game::Update(float)
    std::uintptr_t world_update;      // World::Update(ycUpdateQueueContext*)
    std::uintptr_t update_queue;      // ycUpdateQueue::Update(float)
};

// Returns a stable reference to the offset table for the given build.
// Unknown / unmapped builds return an all-zero table.
const GameOffsets &offsets_for(Build);

// Per-build addresses of the randomizer item-collection functions, as
// load-base-relative offsets. Zero == not mapped for this build.
struct RandoOffsets
{
    std::uintptr_t on_pickup_done; // Items::OnPickupDone(int slot, int itemType, ...)
};

const RandoOffsets &rando_offsets_for(Build);

// Per-build addresses of functions the dev overlay hooks. Zero == not mapped.
// The Vulkan render path resolves its functions dynamically from libvulkan, so
// only the statically-linked SDL input pump needs a build-keyed offset here.
struct OverlayOffsets
{
    std::uintptr_t process_sdl_event; // ProcessSDLEvent(SDL_Event&) — the game's central event
                                      // processor; main's SDL_WaitEvent loop calls it for every
                                      // event (the SDL filter/PeepEvents/PollEvent are NOT the path)
};

const OverlayOffsets &overlay_offsets_for(Build);

} // namespace mth
