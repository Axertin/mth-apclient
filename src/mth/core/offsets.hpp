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

} // namespace mth
