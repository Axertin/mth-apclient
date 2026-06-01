#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "mth/core/ap_events.hpp"

namespace mth
{

// The mod's view of the connected AP room. Game-thread-only, no locks: written
// solely by ApCoordinator (folding ApEvents on the tick), read by game logic.
class ApState
{
  public:
    void apply(const ApEvent &ev);

    [[nodiscard]] bool authenticated() const
    {
        return authenticated_;
    }
    [[nodiscard]] const std::string &status() const
    {
        return status_;
    }
    [[nodiscard]] const std::string &slot_data() const
    {
        return slot_data_;
    }
    [[nodiscard]] int player_slot() const
    {
        return player_slot_;
    }
    [[nodiscard]] bool is_valid_location(std::int64_t id) const
    {
        return valid_locations_.contains(id);
    }
    [[nodiscard]] const std::vector<ReceivedItem> &received_items() const
    {
        return received_items_;
    }
    [[nodiscard]] int last_item_index() const
    {
        return last_item_index_;
    }

  private:
    bool authenticated_{false};
    std::string status_{"Idle"};
    std::string slot_data_{};
    int player_slot_{-1};
    std::set<std::int64_t> valid_locations_{};
    std::vector<ReceivedItem> received_items_{};
    int last_item_index_{-1};
};

} // namespace mth
