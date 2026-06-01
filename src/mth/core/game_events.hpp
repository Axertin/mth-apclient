#pragma once

namespace mth
{

// Semantic game-tick events. The PAL detours (see GameHooks) fire these on the
// game's own thread after forwarding to the original engine function, so any
// game-state access from a handler is naturally serialized with the engine's
// update. Defaults are no-ops so a consumer overrides only the ticks it needs.
//
// This is the seam that hides the platform/build-specific hook addresses from
// gameplay logic: the same on_*() events fire whether they came from a Frida
// detour at a Linux offset or a MinHook detour at a Windows one. Pure (no PAL),
// so handlers can be unit-tested by calling these directly.
struct IGameEvents
{
    virtual ~IGameEvents() = default;

    virtual void on_game_fixed_update()
    {
    } // Game::FixedUpdate() — fixed sim step (dt = 1/targetFPS)
    virtual void on_game_update(float /*dt*/)
    {
    } // Game::Update(dt) — per frame
    virtual void on_world_update()
    {
    } // World::Update — per-level world tick
    virtual void on_update_queue(float /*dt*/)
    {
    } // ycUpdateQueue::Update(dt) — low-level pump (fires per queue)
};

} // namespace mth
