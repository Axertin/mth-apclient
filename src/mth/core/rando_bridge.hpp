#pragma once

#include <cstdint>
#include <set>

#include "mth/core/ap_link.hpp"
#include "mth/core/ap_state.hpp"

namespace mth
{

// AP id scheme (MVP): ap_loc_id = kLocBase + collection idx (0..360),
// ap_item_id = kItemBase + itemTypeId (0..194). Positive, build-pinned.
// The apworld MUST use identical bases.
inline constexpr std::int64_t kLocBase = 1;
inline constexpr std::int64_t kItemBase = 1;

inline constexpr std::int64_t ap_loc_id(int collection_idx)
{
    return kLocBase + collection_idx;
}

// Outbound side: turns a collected collection-slot into a server-validated,
// deduplicated location check. Game-thread-only (no locks); reads ApState
// (also game-thread) and pushes to IApLink (thread-safe command queue).
class RandoBridge
{
  public:
    RandoBridge(IApLink &link, ApState &state);

    // Called from the OnPickupDone detour with the engine's slot arg.
    void on_location_collected(int collection_slot);

  private:
    IApLink &link_;
    ApState &state_;
    std::set<std::int64_t> sent_{};
};

} // namespace mth
