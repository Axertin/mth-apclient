#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "mth/core/ap_link.hpp"

class APClient; // forward-declared; full type only in the .cpp

namespace mth::net
{

// apclientpp-backed IApLink. APClient lives on the net thread only.
class ApLink final : public mth::IApLink
{
  public:
    ApLink();
    ~ApLink() override;
    ApLink(const ApLink &) = delete;
    ApLink &operator=(const ApLink &) = delete;

    void connect(const std::string &server, const std::string &slot, const std::string &password) override;
    void disconnect() override;
    [[nodiscard]] bool is_connected() const override;
    void send_locations(const std::vector<std::int64_t> &location_ids) override;
    void set_goal() override;
    void enable_deathlink(bool on) override;
    void send_death(const std::string &cause) override;
    void report_area(int game_state) override;
    [[nodiscard]] std::vector<mth::ApEvent> drain_events() override;

  private:
    void run();
    void enqueue(std::function<void()> cmd);
    void push_event(mth::ApEvent ev);

    void do_connect(const std::string &server, const std::string &slot, const std::string &password);
    void do_disconnect();
    void setup_handlers(const std::string &slot, const std::string &password);

    std::unique_ptr<APClient> client_; // net thread only
    int last_item_index_{-1};          // net thread only
    std::string slot_name_;            // net thread only; captured on connect for bounce source

    std::atomic<bool> running_{true};
    std::atomic<bool> connected_{false};
    std::atomic<bool> deathlink_{false};
    std::mutex cmd_mutex_;
    std::queue<std::function<void()>> commands_;
    std::mutex event_mutex_;
    std::vector<mth::ApEvent> events_;

    std::thread thread_; // last member: started after all others are initialized
};

} // namespace mth::net
