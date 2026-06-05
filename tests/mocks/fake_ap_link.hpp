#pragma once

#include <utility>
#include <vector>

#include "mth/core/ap_link.hpp"

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
    int goal_calls = 0;
    int disconnect_calls = 0;

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
    void set_goal() override
    {
        ++goal_calls;
    }
    [[nodiscard]] std::vector<mth::ApEvent> drain_events() override
    {
        return std::exchange(pending, {});
    }
};

} // namespace mth::test
