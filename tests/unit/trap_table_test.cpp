#include <set>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/ap/ap_ids.hpp"
#include "mth/core/data/modifier_table.hpp"
#include "mth/core/data/trap_table.hpp"
#include "mth/core/modifier_config.hpp" // kCheatCount

TEST_CASE("trap table: every trap is a live-settable continuous modifier", "[trap][trap_table]")
{
    for (const auto &t : mth::traps())
        REQUIRE(mth::is_safe(t.modifier_index));
}

TEST_CASE("trap table: no trap is on the AP deny list", "[trap][trap_table]")
{
    for (const auto &t : mth::traps())
        REQUIRE_FALSE(mth::is_ap_denied(t.modifier_index));
}

TEST_CASE("trap table: indices are unique and in range", "[trap][trap_table]")
{
    std::set<int> seen;
    for (const auto &t : mth::traps())
    {
        REQUIRE(t.modifier_index >= 0);
        REQUIRE(t.modifier_index < mth::kCheatCount);
        REQUIRE(seen.insert(t.modifier_index).second);
    }
    REQUIRE(seen.size() == mth::traps().size());
}

TEST_CASE("trap table: every trap has a label and a positive duration", "[trap][trap_table]")
{
    for (const auto &t : mth::traps())
    {
        REQUIRE(t.label != nullptr);
        REQUIRE_FALSE(std::string_view(t.label).empty());
        REQUIRE(t.seconds > 0.0f);
    }
}

TEST_CASE("trap table: lookup returns the row for every modifier index in the table", "[trap][trap_table]")
{
    for (const auto &t : mth::traps())
        REQUIRE(mth::trap_for_modifier(t.modifier_index) == &t);

    REQUIRE(mth::trap_for_modifier(19) == nullptr); // a Grant-class modifier is never a trap
    REQUIRE(mth::trap_for_modifier(-1) == nullptr);
    REQUIRE(mth::trap_for_modifier(9999) == nullptr);
}

TEST_CASE("ap_ids: the trap segment is 1000 wide from kTrapItemBase", "[trap][ap_ids]")
{
    REQUIRE(mth::is_trap_item(mth::kTrapItemBase));
    REQUIRE(mth::is_trap_item(mth::kTrapItemBase + 204));
    REQUIRE(mth::is_trap_item(mth::kTrapItemBase + 999));
    REQUIRE_FALSE(mth::is_trap_item(mth::kTrapItemBase - 1));
    REQUIRE_FALSE(mth::is_trap_item(mth::kTrapItemBase + 1000));
    REQUIRE_FALSE(mth::is_trap_item(mth::ap_item_id(9)));
}

TEST_CASE("ap_ids: a trap id carries its modifier index", "[trap][ap_ids]")
{
    REQUIRE(mth::trap_modifier_index(mth::kTrapItemBase + 204) == 204);
    REQUIRE(mth::trap_modifier_index(mth::kTrapItemBase) == 0);
}

TEST_CASE("the trap index set is frozen, because a seed already carries these item ids", "[trap]")
{
    // trap_table.hpp spells out the hazard: each row's modifier_index is the AP item id offset
    // (kTrapItemBase + index), so retiring or renumbering a row repoints an id a live seed hands out
    // and the player receives a different trap than the one that was rolled. Adding a row is safe, so
    // this pins the set rather than the count. Raw numbers on purpose: written as kCheat_ names, an
    // upstream renumber would move the AP ids and still pass.
    for (int index : {15, 174, 190, 191, 192, 193, 195, 197, 202, 203, 204, 205})
    {
        INFO("modifier index " << index << " left the table, retiring the AP item id built from it");
        REQUIRE(mth::trap_for_modifier(index) != nullptr);
    }
}
