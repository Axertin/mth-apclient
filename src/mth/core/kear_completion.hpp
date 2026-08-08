#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "mth/core/ap/ap_events.hpp"
#include "mth/core/ap/ap_ids.hpp"

namespace mth
{

// Collection index of the KeyMiser reward; ap_loc_id() maps it into the AP location id space.
inline constexpr int kKearCompletionLocIdx = 150;

// Vanilla kears are fungible and all carry the one Universal Kear id, so the gate counts receipts.
inline constexpr int kVanillaKearTotal = 50;

// Per-lock kear items (kear_rando=1). Mirrors SingleKears in the apworld's data/items/kears.py, whose
// ITEMS_OFFSET_KEARS is 2000. 2306 is absent deliberately: it is the second engine lock the 2304 item
// removes, not an item in its own right.
inline constexpr std::int64_t kSingleKearItemIds[] = {2019, 2020, 2021, 2022, 2033, 2055, 2069, 2096, 2109, 2115, 2127, 2141, 2151,
                                                      2152, 2157, 2222, 2224, 2225, 2227, 2244, 2246, 2247, 2260, 2262, 2277, 2278,
                                                      2283, 2284, 2285, 2304, 2307, 2318, 2320, 2321, 2336, 2337, 2338, 2358, 2359};
inline constexpr int kSingleKearCount = static_cast<int>(sizeof(kSingleKearItemIds) / sizeof(kSingleKearItemIds[0]));

// Per-area kear items (kear_rando=2), contiguous.
inline constexpr std::int64_t kAreaKearFirstItemId = 2500;
inline constexpr std::int64_t kAreaKearLastItemId = 2516;
inline constexpr int kAreaKearCount = static_cast<int>(kAreaKearLastItemId - kAreaKearFirstItemId + 1);

namespace detail
{
[[nodiscard]] inline bool received_has(const std::vector<ReceivedItem> &received, std::int64_t id) noexcept
{
    return std::any_of(received.begin(), received.end(), [id](const ReceivedItem &it) { return it.item_id == id; });
}
} // namespace detail

// twin: mth/features/kear_completion_tracker.hpp latches the save flag off this.
// True once the run holds every kear the active mode defines (#174). The apworld gates location 150 on
// the same condition, so this must track HasAllKears() in its data/rules/state_rules.py.
[[nodiscard]] inline bool have_all_kears(KearMode mode, const std::vector<ReceivedItem> &received) noexcept
{
    if (mode == KearMode::Vanilla)
    {
        const std::int64_t kear = ap_item_id(kUniversalKearItemType);
        const auto n = std::count_if(received.begin(), received.end(), [kear](const ReceivedItem &it) { return it.item_id == kear; });
        return n >= kVanillaKearTotal;
    }

    if (mode == KearMode::ApItems)
        return std::all_of(std::begin(kSingleKearItemIds), std::end(kSingleKearItemIds),
                           [&received](std::int64_t id) { return detail::received_has(received, id); });

    for (std::int64_t id = kAreaKearFirstItemId; id <= kAreaKearLastItemId; ++id)
        if (!detail::received_has(received, id))
            return false;
    return true;
}

} // namespace mth
