#pragma once

#include <cstdint>
#include <optional>

namespace mth
{

class IApLink;

// Publishes the current packed screen id (area<<16 | room) to AP data storage, but only when it
// changes while connected. A disconnect clears the memory of the last-sent value, so the first
// connected tick afterward always re-publishes (a freshly (re)connecting tracker immediately sees
// correct state). Pure: no PAL/OS dependency.
class AreaReporter
{
  public:
    explicit AreaReporter(IApLink &link);

    // Call once per fixed update. `screen_id` is empty when not yet readable.
    void tick(bool connected, std::optional<std::uint32_t> screen_id);

  private:
    IApLink &link_;
    std::optional<std::uint32_t> last_sent_;
    bool was_connected_{false};
};

} // namespace mth
