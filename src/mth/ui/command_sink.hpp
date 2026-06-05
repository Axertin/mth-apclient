#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mth
{

// Console effect interface. Implemented by App; called on the render thread; must not block.
class ICommandSink
{
  public:
    virtual ~ICommandSink() = default;

    virtual void connect(const std::string &server, const std::string &slot, const std::string &password) = 0;
    virtual void disconnect() = 0;

    [[nodiscard]] virtual std::vector<std::string> status_lines() const = 0;
    [[nodiscard]] virtual std::vector<std::string> item_lines() const = 0;

    virtual void give_item(std::int64_t ap_item_id) = 0; // manual test path; bypasses dedup
};

} // namespace mth
