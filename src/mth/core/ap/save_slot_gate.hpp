#pragma once

namespace mth
{

// AP effects may touch the live save only when it is the one this (seed, slot) is bound to.
[[nodiscard]] inline bool ap_save_gate_open(bool authenticated, bool inbound_ready, int bound_slot, int live_slot)
{
    return authenticated && inbound_ready && bound_slot >= 0 && live_slot == bound_slot;
}

// New-game binding: the live slot to bind, or -1 for "do not bind". Bind only when connected,
// inbound ready, currently unbound, and the live slot index is valid (#124 wrong-save guard).
[[nodiscard]] inline int ap_bind_on_new_game(bool authenticated, bool inbound_ready, int bound_slot, int live_slot)
{
    return (authenticated && inbound_ready && bound_slot < 0 && live_slot >= 0) ? live_slot : -1;
}

// Rescue for AP games started before the gate existed: they have progress but no recorded slot, and
// their new-game edge is long past, so they would stay gated off forever. Adopt the live save once.
// `has_progress` is what keeps this from swallowing the gate whole: a fresh seed's state file is also
// unbound, but it is empty, so it still has to wait for its own new-game edge rather than binding to
// whichever save happens to be loaded at connect.
// `in_game` must mean "gameplay is actually live", not merely "a slot pointer exists": the save-slot
// index reads 0 at the title screen, so without this a connect from the title would bind slot 0.
[[nodiscard]] inline bool ap_bind_legacy_unbound(bool authenticated, bool inbound_ready, int bound_slot, int live_slot, bool has_progress, bool in_game)
{
    return authenticated && inbound_ready && bound_slot < 0 && live_slot >= 0 && has_progress && in_game;
}

} // namespace mth
