#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "mth/core/ap/ap_ids.hpp" // kAbilityItemBase

namespace mth
{

// Non-progressive ability gates, segment kAbilityItemBase (3000). Order is the item-id order;
// do not reorder (ids are durable AP item ids shared with the apworld).
enum class Ability
{
    Burrow,       // #33
    Swim,         // #36
    RopeClimb,    // #34
    BouncePuff,   // #35 (puffs)
    BounceSpring, // #35 (springs)
    Carry,        // #37
    Train,        // #22
    Count
};

inline constexpr int kAbilityCount = static_cast<int>(Ability::Count); // 7

inline constexpr std::int64_t ability_item_id(Ability a)
{
    return kAbilityItemBase + static_cast<int>(a);
}

inline constexpr bool is_ability_item(std::int64_t id)
{
    return id >= kAbilityItemBase && id < kAbilityItemBase + kAbilityCount;
}

// Precondition: is_ability_item(id).
inline constexpr Ability ability_from_item(std::int64_t id)
{
    return static_cast<Ability>(id - kAbilityItemBase);
}

[[nodiscard]] inline std::optional<Ability> ability_from_name(std::string_view n)
{
    if (n == "burrow")
        return Ability::Burrow;
    if (n == "swim")
        return Ability::Swim;
    if (n == "rope")
        return Ability::RopeClimb;
    if (n == "puff")
        return Ability::BouncePuff;
    if (n == "spring")
        return Ability::BounceSpring;
    if (n == "carry")
        return Ability::Carry;
    if (n == "train")
        return Ability::Train;
    return std::nullopt;
}

} // namespace mth
