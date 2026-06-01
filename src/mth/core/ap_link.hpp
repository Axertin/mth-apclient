#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mth/core/ap_events.hpp"

namespace mth
{

// Socket-only transport seam. Implementations enqueue outbound work and surface
// inbound server traffic as ApEvents via drain_events(). No randomizer semantics.
class IApLink
{
  public:
    virtual ~IApLink() = default;

    virtual void connect(const std::string &server, const std::string &slot, const std::string &password) = 0;
    virtual void disconnect() = 0;
    [[nodiscard]] virtual bool is_connected() const = 0;

    virtual void send_locations(const std::vector<std::int64_t> &location_ids) = 0;
    virtual void set_goal() = 0;

    // Game thread pulls all events produced since the last call.
    [[nodiscard]] virtual std::vector<ApEvent> drain_events() = 0;
};

} // namespace mth
