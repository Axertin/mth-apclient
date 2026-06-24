#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "mth/core/ability_ids.hpp"

namespace mth
{

// Pure per-ability gate decision. No game/PAL/IO dependency; the caller supplies the AP-grant lookup
// and whether the active save is the AP slot.
class AbilityGate
{
  public:
    struct GrantQuery
    {
        bool slot_is_ap;
        std::function<bool(std::int64_t item_id)> is_granted;
    };

    void set_randomized(Ability a, bool on)
    {
        randomized_[static_cast<int>(a)] = on;
    }
    [[nodiscard]] bool randomized(Ability a) const
    {
        return randomized_[static_cast<int>(a)];
    }

    [[nodiscard]] bool blocked(Ability a, const GrantQuery &q) const
    {
        return randomized(a) && q.slot_is_ap && !q.is_granted(ability_item_id(a));
    }

  private:
    std::array<bool, kAbilityCount> randomized_{};
};

} // namespace mth
