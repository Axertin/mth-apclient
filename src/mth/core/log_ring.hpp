#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace mth
{

// Fixed-capacity FIFO of log lines, safe for concurrent push (log threads) and
// snapshot (render thread). When full, the oldest line is dropped.
class LogRing
{
  public:
    explicit LogRing(std::size_t capacity = 5000) : capacity_(capacity == 0 ? 1 : capacity)
    {
    }

    void push(std::string_view line);
    void clear();

    // Copies current contents oldest-first. Cheap enough for a per-frame UI.
    [[nodiscard]] std::vector<std::string> snapshot() const;
    [[nodiscard]] std::size_t size() const;

  private:
    mutable std::mutex mu_;
    std::vector<std::string> lines_; // ring; head_ is the oldest index when full
    std::size_t capacity_;
    std::size_t head_{0};
};

} // namespace mth
