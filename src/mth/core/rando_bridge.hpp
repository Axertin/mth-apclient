#pragma once

#include <cstdint>
#include <set>

#include "mth/core/ap_link.hpp"
#include "mth/core/ap_state.hpp"

namespace mth
{

// AP id scheme (MVP): ap_loc_id = kLocBase + collection idx (0..360),
// ap_item_id = kItemBase + itemTypeId (0..194). The apworld MUST use identical bases.
// Items use base 0 (ap_item_id == game itemType, identity): itemType 0 is the engine's
// "None" sentinel, so ap_item_id 0 is reserved/ignored rather than a real grant.
inline constexpr std::int64_t kLocBase = 1;
inline constexpr std::int64_t kItemBase = 0;

inline constexpr std::int64_t ap_loc_id(int collection_idx)
{
    return kLocBase + collection_idx;
}

// Item ids mirror the location scheme: ap_item_id = kItemBase + game itemType
// (itemType indexes the game's s_rItems table, 0..194). With kItemBase == 0 this is the
// identity map. The apworld MUST use the same base. Inverse is used by the inbound granter.
inline constexpr std::int64_t ap_item_id(int item_type)
{
    return kItemBase + item_type;
}

inline constexpr int game_item_type(std::int64_t ap_item_id_)
{
    return static_cast<int>(ap_item_id_ - kItemBase);
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
