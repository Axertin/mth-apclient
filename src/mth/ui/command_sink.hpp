#pragma once

#include <string>
#include <vector>

namespace mth
{

// Effects the console can trigger. App implements this over its link_/state_.
// Methods run on the render thread; implementations must not block.
class ICommandSink
{
  public:
    virtual ~ICommandSink() = default;

    virtual void connect(const std::string &server, const std::string &slot, const std::string &password) = 0;
    virtual void disconnect() = 0;

    // Human-readable lines describing current AP/connection state (for `status`).
    [[nodiscard]] virtual std::vector<std::string> status_lines() const = 0;
    // Human-readable lines, one per received AP item (for `items`).
    [[nodiscard]] virtual std::vector<std::string> item_lines() const = 0;
};

} // namespace mth
