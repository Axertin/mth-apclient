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
    virtual void on_game_update(float /*dt*/)
    {
    } // Game::Update
    virtual void on_world_update()
    {
    } // World::Update
    virtual void on_update_queue(float /*dt*/)
    {
    } // ycUpdateQueue::Update

    // Fired BEFORE World::Update. Spawns must happen here to avoid update-queue hangs.
    virtual void on_world_update_pre()
    {
    }
};

} // namespace mth
