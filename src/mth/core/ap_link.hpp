#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mth/core/ap_events.hpp"

namespace mth
{

// Transport seam: outbound calls enqueue work; inbound traffic surfaces as ApEvents.
class IApLink
{
  public:
    virtual ~IApLink() = default;

    virtual void connect(const std::string &server, const std::string &slot, const std::string &password) = 0;
    virtual void disconnect() = 0;
    [[nodiscard]] virtual bool is_connected() const = 0;

    virtual void send_locations(const std::vector<std::int64_t> &location_ids) = 0;
    virtual void set_goal() = 0;

    virtual void enable_deathlink(bool on) = 0; // tag the connection; call before connect()
    virtual void send_death(const std::string &cause) = 0;

    // Publish the current room/area id to AP data storage (key MTH_level_<team>_<slot>).
    virtual void report_area(int game_state) = 0;

    [[nodiscard]] virtual std::vector<ApEvent> drain_events() = 0;
};

} // namespace mth
