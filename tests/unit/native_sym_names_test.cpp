#include <cstring>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/data/native_sym_names.hpp"

using mth::sym::kNativeSymNames;
using mth::sym::native_sym_name;

TEST_CASE("native_sym_name maps a mangled name to the game's plain name", "[symnames]")
{
    REQUIRE(std::string_view(native_sym_name(mth::sym::pickup_init)) == "Pickup::Init");
    REQUIRE(std::string_view(native_sym_name(mth::sym::hub_fountain_bulb_update)) == "HubFountain::Bulb::Update");
}

TEST_CASE("native_sym_name drops the anonymous-namespace qualification on data symbols", "[symnames]")
{
    // _ZN12_GLOBAL__N_18s_rItemsE carries the anon-namespace prefix; GetSymAddr takes the bare name.
    REQUIRE(std::string_view(native_sym_name(mth::sym::s_r_items)) == "s_rItems");
    REQUIRE(std::string_view(native_sym_name(mth::sym::s_r_item_collection)) == "s_rItemCollection");
}

TEST_CASE("native_sym_name returns null for a name the API does not expose", "[symnames]")
{
    // Not in the documented GetSymAddr list, so it must keep its own resolution path.
    REQUIRE(native_sym_name(mth::sym::on_pickup_done) == nullptr);
    REQUIRE(native_sym_name("g_saveManager") == nullptr);
    REQUIRE(native_sym_name(nullptr) == nullptr);
}

TEST_CASE("the native symbol table has no empty or duplicated entries", "[symnames]")
{
    for (const auto &n : kNativeSymNames)
    {
        REQUIRE(n.mangled != nullptr);
        REQUIRE(n.plain != nullptr);
        REQUIRE(std::strlen(n.mangled) > 0);
        REQUIRE(std::strlen(n.plain) > 0);
        // A plain name must not be mangled: that would mean a row was filled in from the wrong column.
        REQUIRE(std::string_view(n.plain).substr(0, 2) != "_Z");
    }

    for (const auto &a : kNativeSymNames)
    {
        int mangled_hits = 0;
        int plain_hits = 0;
        for (const auto &b : kNativeSymNames)
        {
            mangled_hits += (std::strcmp(a.mangled, b.mangled) == 0) ? 1 : 0;
            plain_hits += (std::strcmp(a.plain, b.plain) == 0) ? 1 : 0;
        }
        REQUIRE(mangled_hits == 1);
        REQUIRE(plain_hits == 1);
    }
}
