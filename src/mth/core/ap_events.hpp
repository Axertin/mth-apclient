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
    bool ossex_start{false}; // slot_data "ossex_start": force the Landing Done modifier (start at Ossex hub)
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
struct ApDeathReceived
{
    std::string cause;
};

using ApEvent = std::variant<ApConnected, ApItemReceived, ApDisconnected, ApConnectionRefused, ApStatusChanged, ApDeathReceived>;

} // namespace mth
