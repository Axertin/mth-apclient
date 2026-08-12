#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "mth/core/ap/ap_events.hpp"

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
        return config_.ossex_start;
    }
    [[nodiscard]] KearMode kear_mode() const // slot_data "kear_rando": how kears are randomized
    {
        return config_.kear_mode;
    }
    // Usable keys are meaningless unless the pool carries Universal Kear items, so every mode but Vanilla
    // pins them to zero. The world-kear collect grant is neutralized in ALL modes (the spot is an AP
    // location, so the server owns its reward).
    [[nodiscard]] bool kear_keys_suppressed() const
    {
        return config_.kear_mode != KearMode::Vanilla;
    }
    // slot_data flags: the named ability is AP-randomized; gate it until its AP item is granted.
    [[nodiscard]] bool burrow_rando() const
    {
        return config_.burrow_rando;
    }
    [[nodiscard]] bool swim_rando() const
    {
        return config_.swim_rando;
    }
    [[nodiscard]] bool rope_rando() const
    {
        return config_.rope_rando;
    }
    [[nodiscard]] bool puff_rando() const
    {
        return config_.puff_rando;
    }
    [[nodiscard]] bool spring_rando() const
    {
        return config_.spring_rando;
    }
    [[nodiscard]] bool carry_rando() const
    {
        return config_.carry_rando;
    }
    [[nodiscard]] bool train_rando() const
    {
        return config_.train_rando;
    }
    [[nodiscard]] int train_pass_cost() const // bones the station's donation machine asks for
    {
        return config_.train_pass_cost;
    }
    [[nodiscard]] bool deathlink() const // slot_data flag: deaths bounce over the AP link
    {
        return config_.deathlink;
    }
    [[nodiscard]] int max_stat_level() const // slot_data "max_stat_level": per-stat level ceiling (10..99)
    {
        return config_.max_stat_level;
    }
    [[nodiscard]] int goal_config() const // slot_data "goal_config": 0=finish, 1=generators, 2=bosses
    {
        return config_.goal_config;
    }
    [[nodiscard]] int goal_generators() const // slot_data "goal_generators": generators needed for the generators goal
    {
        return config_.goal_generators;
    }
    [[nodiscard]] int goal_bosses() const // slot_data "goal_bosses": bosses needed for the bosses goal
    {
        return config_.goal_bosses;
    }
    [[nodiscard]] std::uint64_t broken_generator_mask() const // slot_data "broken_generators": these count toward the goal
    {
        return config_.broken_generator_mask;
    }
    [[nodiscard]] bool wallet_cap() const // slot_data "wallet_cap": cap the bone wallet by received wallet items
    {
        return config_.wallet_cap;
    }
    [[nodiscard]] std::uint32_t lit_generator_lamp_mask() const // slot_data "lit_generators": Ossex fountain lamps to force lit (visual only)
    {
        return config_.lit_generator_lamp_mask;
    }
    [[nodiscard]] bool is_valid_location(std::int64_t id) const
    {
        return valid_locations_.contains(id);
    }
    // slot_data "removed_locations": pruned from the pool, so the client treats them as already
    // collected. Disjoint from valid_locations_ by construction (see apply): the server's view wins.
    [[nodiscard]] bool is_removed_location(std::int64_t id) const
    {
        return removed_locations_.contains(id);
    }
    [[nodiscard]] const std::set<std::int64_t> &removed_locations() const
    {
        return removed_locations_;
    }
    [[nodiscard]] const std::vector<ReceivedItem> &received_items() const
    {
        return received_items_;
    }
    // Drain the server-reported checked location ids accumulated since the last call (Collect / coop).
    // Game-thread only; App reconciles these into the save-state checked set, then this returns empty.
    [[nodiscard]] std::vector<std::int64_t> take_server_checked_pending()
    {
        return std::exchange(server_checked_pending_, {});
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
    // Drop everything a connection accumulates, so the next starts as it would have at launch. Must run
    // before the next connection's events are applied, which is why the ApSessionEnded marker driving it is
    // ordered ahead of ApConnected; running it after would discard the new server's item stream and its
    // authoritative checked-location report.
    void reset_session();

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
    SlotDataConfig config_{}; // pre-connection defaults; every reader gates on authenticated() first
    std::set<std::int64_t> valid_locations_{};
    // config_.removed_locations filtered against valid_locations_ (see apply); config_ keeps the server's unfiltered list.
    std::set<std::int64_t> removed_locations_{};
    std::vector<ReceivedItem> received_items_{};
    std::vector<std::int64_t> server_checked_pending_{}; // server-reported checks awaiting reconcile (game-thread)
    int last_item_index_{-1};
    int console_index_{-1000000}; // synthetic index for console-injected items; decremented, never collides with server's >=0 cursor
};

} // namespace mth
