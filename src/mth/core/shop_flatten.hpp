#pragma once

#include <cstdint>

namespace mth
{

// InteractComponent::OpenShop merges consecutive same-item-type shop entries into one stacked box
// unless the shop's "never stack" bit is set; setting it makes every level its own buyable box (what
// the built-in shuffler produces indirectly via randomized item types). ShopDef flags are a dword at
// +0x228; bit 0x100 is the never-stack control.
inline constexpr std::ptrdiff_t kShopFlagsOff = 0x228;
inline constexpr std::uint32_t kShopNeverStackBit = 0x100;

// Pure: OR the never-stack bit when the mod is active, else pass flags through. No platform deps.
[[nodiscard]] constexpr std::uint32_t apply_flatten_flag(std::uint32_t flags, bool active) noexcept
{
    return active ? (flags | kShopNeverStackBit) : flags;
}

} // namespace mth
