#pragma once

namespace mth
{

// Engine-tick event seam. GameHooks fires these on the game thread after forwarding
// to the original. Pure (no PAL); testable by direct calls.
struct IGameEvents
{
    virtual ~IGameEvents() = default;

    virtual void on_game_fixed_update()
    {
    } // Game::FixedUpdate

    // Fired BEFORE World::Update (via the native "WorldUpdate" mod hook). Spawns must happen here to
    // avoid update-queue hangs.
    virtual void on_world_update_pre()
    {
    }

    // Fired AFTER World::Update (via the native "WorldUpdateEnd" mod hook), once the other systems have
    // run. Carries the ctx's World*, the only one reachable in a menu world. world may be null.
    virtual void on_world_update_end(void * /*world*/)
    {
    }

    // Fired when a World is destroyed (via the native "WorldDestroy" mod hook): exit-to-menu, save
    // reload, shutdown. Drop any cached per-world game pointers here before the game frees them.
    virtual void on_world_destroy()
    {
    }
};

} // namespace mth
