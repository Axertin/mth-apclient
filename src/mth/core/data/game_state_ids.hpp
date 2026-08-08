#pragma once

namespace mth
{

// Gamestate ids, mirrored from the mod API's GAMESTATE_* defines so core stays free of game headers.
// The enum is one flat contiguous run with no kind or flag field, but it is ordered: every world/room
// state sits in [kGameStateFirstWorld, kGameStateLastWorld], and everything from the title screen up is
// a menu, overlay, cinematic or panorama. Menu worlds still tick World::Update, and GetRoomIndex keeps
// returning the last room while one is open, so this range is the only thing separating "in a room"
// from "in a menu".
inline constexpr int kGameStateFirstWorld = 2;   // GAMESTATE_INTROBEACH
inline constexpr int kGameStateLastWorld = 83;   // GAMESTATE_GYM_WORLDLOADTEST2
inline constexpr int kGameStateTitleScreen = 84; // GAMESTATE_TITLE_SCREEN
inline constexpr int kGameStateProfileSelect = 105;

// current_game_state() reports -1 when the API is unavailable, which lands outside the range.
[[nodiscard]] inline constexpr bool is_gameplay_game_state(int gs) noexcept
{
    return gs >= kGameStateFirstWorld && gs <= kGameStateLastWorld;
}

} // namespace mth
