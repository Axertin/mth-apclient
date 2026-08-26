#pragma once

#include <span>

namespace mth
{

// One trap: the modifier it forces on, the game's own shipped English menu label for it, and how
// long it lasts in gameplay seconds.
struct TrapDef
{
    int modifier_index;
    const char *label;
    float seconds;
};

// Every trap runs this long for now. Each row carries its own duration so tuning one trap later is
// a table edit, and so a slot_data duration can multiply a per-trap baseline.
inline constexpr float kDefaultTrapSeconds = 30.0f;

// Each row's modifier_index is also the AP item id offset for that trap (kTrapItemBase + index), and
// the randomizer hands those same ids out, so removing a row or renumbering one retires or repoints
// an id a seed may already carry. Adding a row is safe; changing an existing one is not.
//
// The curated trap set. Curation is deliberate: being a good trap is narrower than either
// is_safe or !is_ap_denied, because a modifier also has to be read every frame (so the effect lands
// without a room change) and has to be worth the interruption. The unit test pins the two
// predicates so the curation cannot drift into something that breaks seed logic.
//
// Deliberately excluded: the prank set (179 to 182), Large Apple (206), Customize Flowers (213) and
// No Wind (175) are read at room load, so a trap would not show until the player walks through a
// door. The color and weather set (185 to 188, 199, 212) reads a value field in the save slot rather
// than a mask bit, which would put save writes back into the path.
[[nodiscard]] std::span<const TrapDef> traps();

// Null when that modifier is not a trap.
[[nodiscard]] const TrapDef *trap_for_modifier(int modifier_index);

} // namespace mth
