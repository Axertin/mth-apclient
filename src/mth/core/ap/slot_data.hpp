#pragma once

#include <cstdint>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "mth/core/ap/ap_ids.hpp"  // kTrainPassCostDefault
#include "mth/core/goal_state.hpp" // kAllGeneratorsMask

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

// Everything the client derives from the server's slot_data blob. The parse below fills it; the ApConnected
// payload and ApState's accessors hand back these fields directly.
struct SlotDataConfig
{
    bool ossex_start{false};               // "ossex_start": force the Landing Done modifier (start at Ossex hub)
    KearMode kear_mode{KearMode::ApItems}; // "kear_rando": how kears are randomized (apworld default = ApItems)
    // "*_rando": the named ability is AP-randomized; gate it until its AP item is granted.
    bool burrow_rando{false};
    bool swim_rando{false};
    bool rope_rando{false};
    bool puff_rando{false};
    bool spring_rando{false};
    bool carry_rando{false};
    bool train_rando{true};
    int train_pass_cost{kTrainPassCostDefault};              // "train_pass_cost": bones the station machine asks for
    bool deathlink{false};                                   // "death_link": bounce/receive deaths over the AP link
    int max_stat_level{99};                                  // "max_stat_level": per-stat level ceiling (clamped 10..99; 99 = game's absolute max)
    int goal_config{0};                                      // "goal_config": 0=finish, 1=generators, 2=bosses
    int goal_generators{99};                                 // "goal_generators": generators needed (default unreachable)
    std::uint64_t broken_generator_mask{kAllGeneratorsMask}; // "broken_generators": these count toward the goal
    int goal_bosses{99};                                     // "goal_bosses": bosses needed (default unreachable)
    bool wallet_cap{false};                                  // "wallet_cap": cap the bone wallet by received wallet items
    // "mirror_switch_rando": the Mirrors End shortcut switches are AP items. On by default: in a session
    // the mod owns the save, so the switches are AP's to drive whether or not the seed mentions them. A
    // seed that does not shuffle them holds all five shut, so send mirror_switch_rando=0 to opt out.
    bool mirror_switch_rando{true};
    std::uint32_t lit_generator_lamp_mask{0}; // "lit_generators": force these Ossex fountain lamps lit (visual only)
    // "removed_locations": ids the apworld pruned from the pool; treated as already collected.
    std::vector<std::int64_t> removed_locations{};
};

// Map a server slot_data blob onto the config. A non-object blob (older/absent slot_data) yields every
// default. Out-of-range and non-integer array entries are warned about and skipped; a value whose JSON
// type does not match the field throws out of nlohmann.
[[nodiscard]] SlotDataConfig parse_slot_data(const nlohmann::json &data);

} // namespace mth
