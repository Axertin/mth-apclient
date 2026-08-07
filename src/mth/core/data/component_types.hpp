#pragma once

#include <cstdint>
#include <string_view>

#include "mth/core/hook_hash.hpp"

// MM_Rtti type ids for MinaModAPI::ComponentIsa. A type id is the same hashlittle2 of the type name
// that the named mod hooks dispatch on, so any type can be identified without an instance to compare
// against. The static_asserts pin the recipe to what the game's own GetTypeId() bodies return.
namespace mth::rtti
{

[[nodiscard]] inline constexpr std::uint64_t type_id(std::string_view type_name) noexcept
{
    return hookhash::hash64(type_name);
}

// An entity is itself a component in the child list, and holds the next level of the scene graph, so
// this is what a traversal recurses on.
inline constexpr std::uint64_t kYcEntity = type_id("ycEntity");
// The Ossex Station train-pass donation machine and the interact component it owns.
inline constexpr std::uint64_t kTicketMachine = type_id("TicketMachine");
inline constexpr std::uint64_t kInteractComponent = type_id("InteractComponent");

static_assert(kYcEntity == 0x8a1d0d54bd371fe0ULL, "ycEntity::GetTypeId()");
static_assert(kTicketMachine == 0xca27866425643ac0ULL, "TicketMachine::GetTypeId()");
static_assert(kInteractComponent == 0x7998225abfacd663ULL, "InteractComponent::GetTypeId()");

} // namespace mth::rtti
