#pragma once

#include <set>
#include <string>
#include <vector>

namespace mth
{

inline constexpr int kCheatCount = 254; // valid modifier indices are 0..253

// Modifiers force-enabled on AP save slots (indices from the verified g_cheats master table).
// Warp Home is always forced; Landing Done (kCheat_StartProgIntro: skip the intro, start at the
// Ossex hub) is forced only when slot_data sets "ossex_start".
inline constexpr int kCheatWarpHome = 121;
inline constexpr int kCheatLandingDone = 128;
inline constexpr int kCheatCheaperBoneUp = 102;

// Result of parsing a modifier-index list. `indices` is the requested set (stable order, deduped).
// `forced` are indices the user opted into despite being on the deny-list (token prefixed `force:`).
struct ModifierRequest
{
    std::vector<int> indices;
    std::set<int> forced;
};

// Parse a comma-separated modifier list. Tokens: a bare integer, or `force:<int>`. Whitespace
// around tokens is trimmed. Out-of-range (not 0..253) and non-numeric tokens are dropped. Order
// is first-seen; duplicates collapse.
[[nodiscard]] ModifierRequest parse_modifier_indices(const std::string &csv);

} // namespace mth
