#pragma once

#include "mth/core/ap_link.hpp"

namespace mth
{

// No-op IApLink for when the net lane is disabled (e.g. LLVM-MinGW, no OpenSSL).
class NullApLink final : public IApLink
{
  public:
    void connect(const std::string &, const std::string &, const std::string &) override
    {
    }
    void disconnect() override
    {
    }
    [[nodiscard]] bool is_connected() const override
    {
        return false;
    }
    void send_locations(const std::vector<std::int64_t> &) override
    {
    }
    void set_goal() override
    {
    }
    [[nodiscard]] std::vector<ApEvent> drain_events() override
    {
        return {};
    }
};

} // namespace mth
