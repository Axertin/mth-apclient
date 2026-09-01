#pragma once

#include "MinaModEnums.h"

namespace mth
{

// Gamestate ids, taken from the pinned mod ABI's GAMESTATE_* defines. The enum is one flat
// contiguous run with no kind or flag field, but it is ordered: every world/room state sits in
// [kGameStateFirstWorld, kGameStateLastWorld], and everything from the title screen up is a menu,
// overlay, cinematic or panorama. Menu worlds still tick World::Update, and GetRoomIndex keeps
// returning the last room while one is open, so this range is the only thing separating "in a room"
// from "in a menu".
inline constexpr int kGameStateFirstWorld = GAMESTATE_INTROBEACH;
inline constexpr int kGameStateLastWorld = GAMESTATE_GYM_WORLDLOADTEST2;
inline constexpr int kGameStateTitleScreen = GAMESTATE_TITLE_SCREEN;
inline constexpr int kGameStateProfileSelect = GAMESTATE_PROFILE_SELECT_MENU;
// The Mirrors End mirror hub, which is its own gamestate rather than part of the Orrery, and small enough
// to gate the shortcut switches on: the apworld's room table lists six rooms in it, all but one of them
// Mirrors End proper.
inline constexpr int kGameStateMirrorHub = GAMESTATE_ASTRAL_ORRERY_MIRROR_HUB;
// The death screen, reached only once a death sequence runs to its end. A Proto Spark save runs the same
// sequence and revives in place instead, so this is what separates a cushioned death from one that stuck.
inline constexpr int kGameStateDeath = GAMESTATE_DEATH;

// current_game_state() reports -1 when the API is unavailable, which lands outside the range.
[[nodiscard]] inline constexpr bool is_gameplay_game_state(int gs) noexcept
{
    return gs >= kGameStateFirstWorld && gs <= kGameStateLastWorld;
}

} // namespace mth
