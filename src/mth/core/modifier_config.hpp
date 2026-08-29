#pragma once

#include <set>
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
inline constexpr int kCheatUnlockBoneUps = 106;

// A modifier set to enforce. `indices` is the requested set, deduped. `forced` are indices to apply
// even though they are on the deny-list, which the caller opts into per index.
struct ModifierRequest
{
    std::vector<int> indices;
    std::set<int> forced;
};

} // namespace mth
