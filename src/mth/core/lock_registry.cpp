#include "mth/core/lock_registry.hpp"

#include <sstream>

namespace mth
{

void LockRegistry::set_removed(int slot)
{
    if (slot < 0)
        return;
    std::lock_guard<std::mutex> lk(mtx_);
    removed_.insert(slot);
}

void LockRegistry::add_from_list(const std::string &csv)
{
    std::stringstream ss(csv);
    std::string tok;
    while (std::getline(ss, tok, ','))
    {
        try
        {
            set_removed(std::stoi(tok));
        }
        catch (...)
        {
            // skip non-numeric tokens
        }
    }
}

bool LockRegistry::is_removed(int slot) const
{
    if (slot < 0)
        return false;
    std::lock_guard<std::mutex> lk(mtx_);
    return removed_.count(slot) != 0;
}

std::vector<int> LockRegistry::removed_slots() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return std::vector<int>(removed_.begin(), removed_.end());
}

} // namespace mth
