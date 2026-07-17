#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "mth/core/broadcast.hpp"
#include "mth/core/scout_registry.hpp"

namespace mth
{

// slot_data "kear_rando". Vanilla puts Universal Kear items (itemType 63) in the pool and they must grant
// real usable keys; the AP-item modes remove each lock (or each area's locks) with a dedicated AP item, so
// usable keys carry no meaning and stay pinned at zero. Kear pickup spots are AP locations in every mode.
enum class KearMode : int
{
    Vanilla = 0,
    ApItems = 1,
    AreaApItems = 2,
};

[[nodiscard]] constexpr KearMode kear_mode_from_slot_data(int value) noexcept
{
    return value == 0 ? KearMode::Vanilla : (value == 2 ? KearMode::AreaApItems : KearMode::ApItems);
}

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
    bool ossex_start{false};               // slot_data "ossex_start": force the Landing Done modifier (start at Ossex hub)
    KearMode kear_mode{KearMode::ApItems}; // slot_data "kear_rando": how kears are randomized (apworld default = ApItems)
    // slot_data "*_rando": the named ability is AP-randomized; gate it until its AP item is granted.
    bool burrow_rando{false};
    bool swim_rando{false};
    bool rope_rando{false};
    bool puff_rando{false};
    bool spring_rando{false};
    bool carry_rando{false};
    bool train_rando{true};
    bool deathlink{false};                    // slot_data "death_link": bounce/receive deaths over the AP link
    int max_stat_level{99};                   // slot_data "max_stat_level": per-stat level ceiling (clamped 10..99; 99 = game's absolute max)
    int goal_config{0};                       // slot_data "goal_config": 0=finish, 1=generators, 2=bosses
    int goal_generators{99};                  // slot_data "goal_generators": generators needed (default unreachable)
    int goal_bosses{99};                      // slot_data "goal_bosses": bosses needed (default unreachable)
    bool wallet_cap{false};                   // slot_data "wallet_cap": cap the bone wallet by received wallet items
    std::uint32_t lit_generator_lamp_mask{0}; // slot_data "lit_generators": force these Ossex fountain lamps lit (visual only)
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

using ApEvent = std::variant<ApConnected, ApConnecting, ApItemReceived, ApLocationsChecked, ApDisconnected, ApConnectionRefused, ApStatusChanged,
                             ApDeathReceived, ApPrintBroadcast, ApScoutInfo>;

} // namespace mth
