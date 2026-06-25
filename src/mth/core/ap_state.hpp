#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "mth/core/ap_events.hpp"

namespace mth
{

enum class ConnectionPhase
{
    Disconnected,
    Connecting,
    Connected,
    Error
};

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
    [[nodiscard]] ConnectionPhase phase() const
    {
        return phase_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::string detail() const
    {
        std::lock_guard<std::mutex> lk(detail_mutex_);
        return detail_;
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
    [[nodiscard]] bool ossex_start() const // slot_data flag: force the Landing Done modifier
    {
        return ossex_start_;
    }
    [[nodiscard]] bool kear_rando() const // slot_data flag: suppress the vanilla world-kear grant
    {
        return kear_rando_;
    }
    // slot_data flags: the named ability is AP-randomized; gate it until its AP item is granted.
    [[nodiscard]] bool burrow_rando() const
    {
        return burrow_rando_;
    }
    [[nodiscard]] bool swim_rando() const
    {
        return swim_rando_;
    }
    [[nodiscard]] bool rope_rando() const
    {
        return rope_rando_;
    }
    [[nodiscard]] bool puff_rando() const
    {
        return puff_rando_;
    }
    [[nodiscard]] bool spring_rando() const
    {
        return spring_rando_;
    }
    [[nodiscard]] bool carry_rando() const
    {
        return carry_rando_;
    }
    [[nodiscard]] bool train_rando() const
    {
        return train_rando_;
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
    // True if item_id appears in the received inventory (reconnect-durable: server resends full list).
    [[nodiscard]] bool has_received(std::int64_t item_id) const
    {
        for (const auto &it : received_items_)
            if (it.item_id == item_id)
                return true;
        return false;
    }

  private:
    // Append a received item, applying the puff->spring bounce alias (see ap_state.cpp).
    void push_received(const ReceivedItem &item);
    // Store the phase and clear detail_ under its lock (non-error transitions).
    void set_phase(ConnectionPhase p);

    bool authenticated_{false};
    std::string status_{"Idle"};
    std::atomic<ConnectionPhase> phase_{ConnectionPhase::Disconnected};
    std::string detail_{};            // human-readable error/status detail for the login window
    mutable std::mutex detail_mutex_; // guards detail_ across the game/render thread boundary
    std::string seed_{};
    std::string slot_data_{};
    int player_slot_{-1};
    bool ossex_start_{false};
    bool kear_rando_{false};
    bool burrow_rando_{false};
    bool swim_rando_{false};
    bool rope_rando_{false};
    bool puff_rando_{false};
    bool spring_rando_{false};
    bool carry_rando_{false};
    bool train_rando_{false};
    std::set<std::int64_t> valid_locations_{};
    std::vector<ReceivedItem> received_items_{};
    int last_item_index_{-1};
    int console_index_{-1000000}; // synthetic index for console-injected items; decremented, never collides with server's >=0 cursor
};

} // namespace mth
