#include <filesystem>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/ap_save_state.hpp"

TEST_CASE("ApSaveState round-trips both sets through a file", "[ap_save_state]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_test_state.txt";
    std::filesystem::remove(path);

    {
        mth::ApSaveState s(path);
        REQUIRE_FALSE(s.is_granted(7));
        s.mark_granted(7);
        s.mark_granted(7);
        s.mark_checked(3);
        REQUIRE(s.is_granted(7));
        REQUIRE(s.is_checked(3));
        s.save();
    }
    {
        mth::ApSaveState s(path);
        REQUIRE(s.is_granted(7));
        REQUIRE_FALSE(s.is_granted(8));
        REQUIRE(s.is_checked(3));
    }
    std::filesystem::remove(path);
}

TEST_CASE("ApSaveState on a missing file starts empty", "[ap_save_state]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_no_such_state.txt";
    std::filesystem::remove(path);
    mth::ApSaveState s(path);
    REQUIRE_FALSE(s.is_granted(0));
    REQUIRE_FALSE(s.is_checked(0));
}

TEST_CASE("ApSaveState round-trips the game save slot", "[ap_save_state]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_slot_state.txt";
    std::filesystem::remove(path);

    {
        mth::ApSaveState s(path);
        REQUIRE(s.game_slot() == -1); // unknown by default
        s.set_game_slot(3);
        REQUIRE(s.game_slot() == 3);
        s.save();
    }
    {
        mth::ApSaveState s(path);
        REQUIRE(s.game_slot() == 3);
    }
    std::filesystem::remove(path);
}

TEST_CASE("ApSaveState exposes the checked set for flush", "[ap_save_state]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_checked_accessor.state";
    std::filesystem::remove(path);
    mth::ApSaveState s(path);
    s.mark_checked(7);
    s.mark_checked(3);
    s.mark_checked(7);
    REQUIRE(s.checked() == std::set<int>{3, 7});
}
