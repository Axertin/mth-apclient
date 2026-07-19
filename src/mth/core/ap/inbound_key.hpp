#pragma once

#include <string>
#include <string_view>

namespace mth
{

// Identity of the per-(seed, slot) inbound save-state file: "ap_<seed>_<slot>.state".
// Two AP connections address the same durable checked-set iff their keys match.
[[nodiscard]] inline std::string inbound_state_key(std::string_view seed, int slot)
{
    return "ap_" + std::string(seed) + "_" + std::to_string(slot) + ".state";
}

// True when App must (re)build save_state_ + the inbound granter: either nothing is loaded
// yet, or the connected seed/slot differs from the loaded one. The seed-change case is #124:
// a fresh connect to a different server must re-key the save-state so the resend flushes that
// server's checked-set, not the previous seed's stale one.
[[nodiscard]] inline bool inbound_needs_rebuild(bool ready, std::string_view loaded_key, std::string_view current_key)
{
    return !ready || loaded_key != current_key;
}

} // namespace mth
