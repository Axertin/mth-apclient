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

TEST_CASE("trap table: lookup by modifier index", "[trap][trap_table]")
{
    const mth::TrapDef *d = mth::trap_for_modifier(204);
    REQUIRE(d != nullptr);
    REQUIRE(d->modifier_index == 204);
    REQUIRE(std::string_view(d->label) == "Mirror Mode");

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
