#include <catch2/catch_test_macros.hpp>

#include "mth/hooks/game_tables.hpp"

// is_item_keyed_collected_kind identifies storage-kinds whose "collected" state is a global per-itemType
// have-item bit (kinds 1/9/11) rather than the location's own bit-index (kinds 8/12/17/19). The former
// alias item-ownership: an out-of-order AP grant of the vanilla item marks the location collected, so the
// chest spawns already-open (issue #61). Those AP locations must report the AP checked-state instead.

TEST_CASE("is_item_keyed_collected_kind: global have-item-bit kinds alias item ownership", "[game_tables]")
{
    CHECK(mth::tables::is_item_keyed_collected_kind(1));  // vessels / containers / weapon tiers
    CHECK(mth::tables::is_item_keyed_collected_kind(9));  // subweapon / spell / ability unlock flags + capacity pieces
    CHECK(mth::tables::is_item_keyed_collected_kind(11)); // trinkets (the reported #61 case)
}

TEST_CASE("is_item_keyed_collected_kind: location-bit-keyed kinds do not alias", "[game_tables]")
{
    CHECK_FALSE(mth::tables::is_item_keyed_collected_kind(8));  // kear
    CHECK_FALSE(mth::tables::is_item_keyed_collected_kind(12)); // bonestone
    CHECK_FALSE(mth::tables::is_item_keyed_collected_kind(17)); // lock-unlock bits
    CHECK_FALSE(mth::tables::is_item_keyed_collected_kind(19)); // fish
    CHECK_FALSE(mth::tables::is_item_keyed_collected_kind(0));  // none / no-grant
}
