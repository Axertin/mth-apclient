#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "mth/core/ap/slot_data.hpp"
#include "mth/core/broadcast.hpp"
#include "mth/core/scout_registry.hpp"

namespace mth
{

struct ReceivedItem
{
    std::int64_t item_id{};
    int index{}; // monotonic dedup cursor
    int player_from{};
    unsigned flags{};
};

// Inbound events: produced by the net thread, applied on the game thread.
struct ApConnected
{
    std::string seed;      // persistence key (with player_slot)
    std::string slot_data; // raw JSON, parsed later
    int player_slot{-1};
    std::vector<std::int64_t> checked_locations;
    std::vector<std::int64_t> missing_locations;
    SlotDataConfig config{}; // everything read out of slot_data (deathlink already force-off adjusted)
};
struct ApItemReceived
{
    ReceivedItem item;
};
// Locations the SERVER reports checked (Connected full set + RoomUpdate deltas): Collect, same-slot
// coop, or connect-time self-heal. Reconciled locally without re-sending.
struct ApLocationsChecked
{
    std::vector<std::int64_t> ap_location_ids;
};
struct ApConnecting
{
};
struct ApDisconnected
{
};
struct ApConnectionRefused
{
    std::vector<std::string> errors;
};
struct ApStatusChanged
{
    std::string text;
};
struct ApDeathReceived
{
    std::string source; // sending slot name, for attributing the death to a player
    std::string cause;
};
// Filtered + pre-rendered on the net thread (apclientpp resolution is net-thread-only); the
// coordinator forwards it to the banner.
struct ApPrintBroadcast
{
    std::vector<BannerSegment> segments;
};
// Scouted shop locations, resolved to display strings on the net thread (apclientpp resolution is
// net-thread-only), carried to the game thread to fill the ScoutRegistry.
struct ApScoutInfo
{
    std::vector<ScoutInfo> locations;
};

// The server authenticated a different (seed, player_slot) than this process was on. Emitted immediately
// before that connection's ApConnected so the old session is dropped in stream order, never after the new
// one's data lands. A reconnect to the same seed+slot does not emit this: that state is still valid.
struct ApSessionEnded
{
};

using ApEvent = std::variant<ApConnected, ApConnecting, ApItemReceived, ApLocationsChecked, ApDisconnected, ApConnectionRefused, ApStatusChanged,
                             ApDeathReceived, ApPrintBroadcast, ApScoutInfo, ApSessionEnded>;

} // namespace mth
