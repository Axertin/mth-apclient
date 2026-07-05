#pragma once

#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace mth
{

// twin: mth/features/lock_hooks.hpp + chest_hooks.hpp consume this registry.
// Thread-safe set of KeyBlock location slots the mod should pre-open. Negative slots
// (cosmetic locks with no save index) are never accepted. Written by AP/console seams,
// read by the game-thread pre-seed pass.
class LockRegistry
{
  public:
    void set_removed(int slot);                 // no-op for slot < 0
    void add_from_list(const std::string &csv); // "3,7,20"
    [[nodiscard]] bool is_removed(int slot) const;
    [[nodiscard]] std::vector<int> removed_slots() const; // copy under lock, for the seed pass

  private:
    mutable std::mutex mtx_;
    std::set<int> removed_;
};

} // namespace mth
