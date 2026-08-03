#pragma once

#include "mth/core/ap_gate.hpp"

namespace mth
{

// Static half of the gate's inputs; App fills in liveness as it arrives. Each probe logs its own
// result, so a refusal names the check that failed. Call after pal::init_hook_engine() and before
// any mod-side write to game memory: tables::repurpose_dummy_item() would otherwise leave this
// validating our own patch.
[[nodiscard]] GateInputs run_static_gate_probes(unsigned int game_revision);

} // namespace mth
