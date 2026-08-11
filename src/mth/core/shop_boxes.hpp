#pragma once

namespace mth
{

// Real shops hold well under this; a larger value means the field being read is not a row count. The
// risk here is one-sided now that the mode gate catches the appearance menu: this can no longer prevent
// a crash, only false-negative a shop the mod's own flatten feature expanded past the bound, which costs
// scouted text on that shop and nothing else.
inline constexpr int kMaxShopBoxes = 64;

// Rows of the ShopMenu box array that are safe to walk. The array is malloc'd and never zeroed, and its
// count only becomes meaningful once SetupBoxes has filled it, so a menu that returned early leaves both
// holding whatever was on the heap. Bail on an out-of-range count rather than clamping: a clamp would
// still walk an array that may not be a box array at all.
[[nodiscard]] constexpr int shop_box_walk_count(int rows) noexcept
{
    return (rows > 0 && rows <= kMaxShopBoxes) ? rows : 0;
}

} // namespace mth
