#pragma once

#include <utility>
#include <vector>

#include "mth/core/ap/ap_link.hpp"

namespace mth::test
{

// In-memory IApLink for unit tests.
class FakeApLink final : public mth::IApLink
{
  public:
    std::vector<mth::ApEvent> pending;
    bool connected = false;

    int connect_calls = 0;
    std::string last_server, last_slot, last_password;
    std::vector<std::int64_t> sent_locations;
    std::vector<std::int64_t> scouted_locations;
    int goal_calls = 0;
    int disconnect_calls = 0;
    bool deathlink_enabled = false;
    std::vector<std::string> sent_deaths;
    std::vector<int> reported_areas;

    void connect(const std::string &server, const std::string &slot, const std::string &password) override
    {
        ++connect_calls;
        last_server = server;
        last_slot = slot;
        last_password = password;
    }
    void disconnect() override
    {
        ++disconnect_calls;
        connected = false;
    }
    [[nodiscard]] bool is_connected() const override
    {
        return connected;
    }
    void send_locations(const std::vector<std::int64_t> &ids) override
    {
        for (auto id : ids)
            sent_locations.push_back(id);
    }
    void scout_locations(const std::vector<std::int64_t> &ids) override
    {
        for (auto id : ids)
            scouted_locations.push_back(id);
    }
    void set_goal() override
    {
        ++goal_calls;
    }
    void enable_deathlink(bool on) override
    {
        deathlink_enabled = on;
    }
    void send_death(const std::string &cause) override
    {
        sent_deaths.push_back(cause);
    }
    void report_area(int game_state) override
    {
        reported_areas.push_back(game_state);
    }
    [[nodiscard]] std::vector<mth::ApEvent> drain_events() override
    {
        return std::exchange(pending, {});
    }
};

} // namespace mth::test
