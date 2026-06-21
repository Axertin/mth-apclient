#pragma once

#include <cstdint>
#include <optional>

namespace mth
{

class IApLink;

// Publishes the current room/area id to AP data storage, but only when it changes
// while connected. A disconnect clears the memory of the last-sent value, so the
// first connected tick afterward always re-publishes (a freshly (re)connecting
// tracker immediately sees correct state). Pure: no PAL/OS dependency.
class AreaReporter
{
  public:
    explicit AreaReporter(IApLink &link);

    // Call once per fixed update. `game_state` is empty when the id is not yet readable.
    void tick(bool connected, std::optional<std::uint32_t> game_state);

  private:
    IApLink &link_;
    std::optional<std::uint32_t> last_sent_;
    bool was_connected_{false};
};

} // namespace mth
