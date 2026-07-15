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

} // namespace mth
