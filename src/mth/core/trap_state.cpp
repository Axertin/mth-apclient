#include "mth/core/trap_state.hpp"

#include <algorithm>

namespace mth
{

void TrapState::arm(int index, float seconds)
{
    if (seconds <= 0.0f)
        return;
    for (auto &t : traps_)
    {
        if (t.index == index)
        {
            t.remaining = std::max(t.remaining, seconds);
            return;
        }
    }
    traps_.push_back({index, seconds});
}

std::vector<int> TrapState::advance(float dt)
{
    std::vector<int> expired;
    if (dt <= 0.0f)
        return expired;
    for (auto &t : traps_)
    {
        t.remaining -= dt;
        if (t.remaining <= 0.0f)
            expired.push_back(t.index);
    }
    std::erase_if(traps_, [](const ActiveTrap &t) { return t.remaining <= 0.0f; });
    return expired;
}

std::vector<int> TrapState::active() const
{
    std::vector<int> out;
    out.reserve(traps_.size());
    for (const auto &t : traps_)
        out.push_back(t.index);
    return out;
}

std::vector<ActiveTrap> TrapState::active_with_remaining() const
{
    return traps_;
}

std::vector<int> TrapState::clear()
{
    std::vector<int> out = active();
    traps_.clear();
    return out;
}

} // namespace mth
