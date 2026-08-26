#include <catch2/catch_test_macros.hpp>

#include "mth/core/data/game_state_ids.hpp"

TEST_CASE("game_state_ids: gameplay range is inclusive of both ends", "[game_state]")
{
    REQUIRE(mth::is_gameplay_game_state(mth::kGameStateFirstWorld));
    REQUIRE(mth::is_gameplay_game_state(mth::kGameStateLastWorld));
    REQUIRE(mth::is_gameplay_game_state((mth::kGameStateFirstWorld + mth::kGameStateLastWorld) / 2));
}

TEST_CASE("game_state_ids: menu, cinematic, and sentinel ids are not gameplay", "[game_state]")
{
    REQUIRE_FALSE(mth::is_gameplay_game_state(mth::kGameStateFirstWorld - 1));
    REQUIRE_FALSE(mth::is_gameplay_game_state(mth::kGameStateLastWorld + 1));
    REQUIRE_FALSE(mth::is_gameplay_game_state(mth::kGameStateTitleScreen));
    REQUIRE_FALSE(mth::is_gameplay_game_state(mth::kGameStateProfileSelect));
    REQUIRE_FALSE(mth::is_gameplay_game_state(-1)); // current_game_state()'s "API unavailable" sentinel
}
