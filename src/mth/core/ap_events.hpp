#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace mth
{

struct ReceivedItem
{
    std::int64_t item_id{};
    int index{}; // AP's monotonic received-items index (dedup cursor)
    int player_from{};
    unsigned flags{};
};

// Inbound events: produced by the transport (net thread), folded into ApState
// on the game thread by ApCoordinator. Plain values - no nlohmann/apclientpp.
struct ApConnected
{
    std::string slot_data; // raw slot_data JSON text (parsed later by game logic)
    int player_slot{-1};
    std::vector<std::int64_t> checked_locations;
    std::vector<std::int64_t> missing_locations;
};
struct ApItemReceived
{
    ReceivedItem item;
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

using ApEvent = std::variant<ApConnected, ApItemReceived, ApDisconnected, ApConnectionRefused, ApStatusChanged>;

} // namespace mth
