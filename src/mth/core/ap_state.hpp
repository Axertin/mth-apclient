#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "mth/core/ap_events.hpp"

namespace mth
{

// Game-thread-only view of the AP room. Written by ApCoordinator, read by game logic.
class ApState
{
  public:
    void apply(const ApEvent &ev);

    // Dev/console seam: append a received item not from the socket. Uses a private negative index so
    // it never collides with or advances the server's monotonic cursor.
    void inject_received_item(std::int64_t item_id);

    [[nodiscard]] bool authenticated() const
    {
        return authenticated_;
    }
    [[nodiscard]] const std::string &status() const
    {
        return status_;
    }
    [[nodiscard]] const std::string &seed() const
    {
        return seed_;
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
    std::string seed_{};
    std::string slot_data_{};
    int player_slot_{-1};
    std::set<std::int64_t> valid_locations_{};
    std::vector<ReceivedItem> received_items_{};
    int last_item_index_{-1};
    int console_index_{-1000000}; // synthetic index for console-injected items; decremented, never collides with server's >=0 cursor
};

} // namespace mth
