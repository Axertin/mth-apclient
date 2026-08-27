#pragma once

#include <cstdint>

namespace mth
{

class ApState;

// The Mirrors End shortcut switches as AP items (#28). Each of the five sits in a locked mirror room and
// vanilla is hit to open the room and raise its colored platforms; under AP the matching item does that
// instead, and hitting one by hand is undone.
//
// Both halves are the same write. A switch's live on/off state is one int in its world's SwitchManager,
// which the switch itself polls and follows, so assigning the granted set each pass grants what AP sent
// and reverts what the player let off, the way the train-line clamp handles station footfall.
void tick_mirror_switches(const ApState &state, bool slot_ok);

// Room teardown. Every room is its own World and the switches die with it, so the pointers this feature
// holds are dropped here rather than left for the next tick's own checks to notice.
void mirror_switches_on_world_destroy();

// Dev probe: log every rainbow switch in the room, with the level-data fields that tell them apart.
// Console verb `switches`, applied on the next tick because the walk calls into the game.
void request_switch_probe();

// Offline test: drive the switches without an AP session. This deliberately bypasses the bound-save gate,
// and the clamp is a durable write, so it belongs on a scratch save and nowhere else.
void set_mirror_switches_override_flag(bool on);

} // namespace mth
