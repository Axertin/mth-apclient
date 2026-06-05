#include "mth/core/log_ring.hpp"

namespace mth
{

void LogRing::push(std::string_view line)
{
    std::lock_guard<std::mutex> lock(mu_);
    if (lines_.size() < capacity_)
    {
        lines_.emplace_back(line);
        return;
    }
    lines_[head_] = std::string(line);
    head_ = (head_ + 1) % lines_.size();
}

void LogRing::clear()
{
    std::lock_guard<std::mutex> lock(mu_);
    lines_.clear();
    head_ = 0;
}

std::vector<std::string> LogRing::snapshot() const
{
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<std::string> out;
    out.reserve(lines_.size());
    for (std::size_t i = 0; i < lines_.size(); ++i)
        out.push_back(lines_[(head_ + i) % lines_.size()]);
    return out;
}

std::size_t LogRing::size() const
{
    std::lock_guard<std::mutex> lock(mu_);
    return lines_.size();
}

} // namespace mth
