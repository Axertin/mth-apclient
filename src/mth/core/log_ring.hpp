#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace mth
{

// Fixed-capacity concurrent ring: push from log threads, snapshot from render thread.
class LogRing
{
  public:
    explicit LogRing(std::size_t capacity = 5000) : capacity_(capacity == 0 ? 1 : capacity)
    {
    }

    void push(std::string_view line);
    void clear();

    [[nodiscard]] std::vector<std::string> snapshot() const; // oldest-first copy
    [[nodiscard]] std::size_t size() const;

  private:
    mutable std::mutex mu_;
    std::vector<std::string> lines_; // ring buffer; head_ = oldest index when full
    std::size_t capacity_;
    std::size_t head_{0};
};

} // namespace mth
