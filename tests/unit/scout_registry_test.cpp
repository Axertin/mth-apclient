#include <string>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/scout_registry.hpp"

TEST_CASE("ScoutRegistry: lookup returns nullptr for unknown slot", "[scout]")
{
    mth::ScoutRegistry reg;
    REQUIRE(reg.lookup(5) == nullptr);
}

TEST_CASE("ScoutRegistry: record then lookup returns the entry", "[scout]")
{
    mth::ScoutRegistry reg;
    reg.record(mth::ScoutInfo{7, "Progressive Sword", "Alice", "Some Game", 1u, false});
    const mth::ScoutInfo *e = reg.lookup(7);
    REQUIRE(e != nullptr);
    REQUIRE(e->item_name == "Progressive Sword");
    REQUIRE(e->player_alias == "Alice");
    REQUIRE(e->item_flags == 1u);
    REQUIRE_FALSE(e->is_self);
}

TEST_CASE("ScoutRegistry: record overwrites the same slot; clear empties", "[scout]")
{
    mth::ScoutRegistry reg;
    reg.record(mth::ScoutInfo{7, "Old", "A", "G", 0u, false});
    reg.record(mth::ScoutInfo{7, "New", "B", "H", 4u, true});
    REQUIRE(reg.lookup(7)->item_name == "New");
    REQUIRE(reg.lookup(7)->is_self);
    reg.clear();
    REQUIRE(reg.lookup(7) == nullptr);
}

TEST_CASE("format_scout_desc: the other-player form names the recipient and their game", "[scout]")
{
    const std::string d = mth::format_scout_desc(mth::ScoutInfo{1, "Sword", "Alice", "Some Game", 0u, false});
    REQUIRE(d.find("Alice") != std::string::npos);
    REQUIRE(d.find("Some Game") != std::string::npos);
}

TEST_CASE("format_scout_desc: the self form names neither the alias nor the game", "[scout]")
{
    // Our own slot still carries an alias and a game, so a self-scout that fell through to the
    // other-player branch would advertise "for Mina (Mina the Hollower)" in the shop.
    const std::string d = mth::format_scout_desc(mth::ScoutInfo{1, "Sword", "Mina", "Mina the Hollower", 0u, true});
    REQUIRE(d.find("Mina") == std::string::npos);
    REQUIRE(d.find("Mina the Hollower") == std::string::npos);
}
