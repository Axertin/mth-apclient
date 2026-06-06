#pragma once

namespace pal
{

// World-space player position from a PlayerTrackable*. false if unavailable.
// Windows reads base fields directly; calling the real GetPos faults off its own call sites.
bool read_player_position(const void *trackable, float out[3]);

// Pickup* base from the `this` passed to a Pickup::OnPickup hook.
// MSVC: that `this` is the PickupListener MI subobject, not the Pickup base.
void *pickup_base_from_onpickup(void *onpickup_this);

} // namespace pal
