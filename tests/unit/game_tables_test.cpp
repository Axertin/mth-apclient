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

// is_armor_upgrade_itemtype flags the two itemTypes whose vanilla effect is applied inside Items::OnPickup
// (before the hooked OnPickupDone), so the OnPickup detour must suppress them for AP locations (issue #71).
TEST_CASE("is_armor_upgrade_itemtype: vest and damage armor only", "[game_tables]")
{
    CHECK(mth::tables::is_armor_upgrade_itemtype(0x4f));       // Vitality Vest (ArmorUpgrade_Health), the reported case
    CHECK(mth::tables::is_armor_upgrade_itemtype(0x50));       // ArmorUpgrade_Damage
    CHECK_FALSE(mth::tables::is_armor_upgrade_itemtype(0x45)); // health capacity piece (handled via UpgradeState)
    CHECK_FALSE(mth::tables::is_armor_upgrade_itemtype(0x4e));
    CHECK_FALSE(mth::tables::is_armor_upgrade_itemtype(0x51));
    CHECK_FALSE(mth::tables::is_armor_upgrade_itemtype(0));
}

// should_redirect_collected_query decides whether the IsItemCollected override redirects to the AP
// checked-state. It redirects for capacity-upgrade (#8) and item-keyed have-bit (#61) AP locations, but
// NOT for an ownership query (IsItemCollected param5/b5 = true) on a weapon-kind (1) location: the
// weapon-swap chest reads ownership via IsItemCollected with b5=true and needs the real have-item bit, or
// a weapon received (not collected at its own location) from another player is hidden from the chest.

TEST_CASE("should_redirect_collected_query: non-aliasing locations never redirect", "[game_tables]")
{
    // kind 0 / location-bit-keyed kinds, not capacity -> pass through regardless of query context
    CHECK_FALSE(mth::tables::should_redirect_collected_query(/*is_capacity=*/false, /*kind=*/0, /*ownership=*/false));
    CHECK_FALSE(mth::tables::should_redirect_collected_query(false, 8, false));  // kear
    CHECK_FALSE(mth::tables::should_redirect_collected_query(false, 12, true));  // bonestone
    CHECK_FALSE(mth::tables::should_redirect_collected_query(false, 19, false)); // fish
}

TEST_CASE("should_redirect_collected_query: location-collected queries redirect (issues #8/#61)", "[game_tables]")
{
    CHECK(mth::tables::should_redirect_collected_query(/*is_capacity=*/true, /*kind=*/9, /*ownership=*/false)); // #8 boss-rose
    CHECK(mth::tables::should_redirect_collected_query(false, 1, false));                                       // weapon/vessel chest-open (#61)
    CHECK(mth::tables::should_redirect_collected_query(false, 9, false));                                       // subweapon chest-open (#61)
    CHECK(mth::tables::should_redirect_collected_query(false, 11, false));                                      // trinket chest-open (#61)
}

TEST_CASE("should_redirect_collected_query: ownership query on a weapon (kind 1) passes through", "[game_tables]")
{
    // THE FIX: weapon-swap chest ownership read must see the real have-item bit, not AP checked-state.
    CHECK_FALSE(mth::tables::should_redirect_collected_query(/*is_capacity=*/false, /*kind=*/1, /*ownership=*/true));
}

TEST_CASE("should_redirect_collected_query: ownership query on non-weapon kinds still redirects", "[game_tables]")
{
    // Only kind 1 has the weapon-swap chest; subweapons/trinkets/capacity keep the confirmed #61/#8/#48 behavior.
    CHECK(mth::tables::should_redirect_collected_query(false, 9, true));  // subweapon (shop #48 unaffected)
    CHECK(mth::tables::should_redirect_collected_query(false, 11, true)); // trinket
    CHECK(mth::tables::should_redirect_collected_query(true, 9, true));   // capacity piece (#8)
}

// kear_reconciled_spent backs the reload-durable kear key cancel. usable keys = popcount(collected bits)
// - spent; under kear_rando all kears are AP-controlled so usable must stay 0. The collect-time spent bump
// is not rebuilt on reload (the collected bitfield is), so on load spent lags and a free key leaks (the
// reported "one kear on load"). Reconciliation raises spent up to popcount but never lowers it, so real
// spends and an already-balanced count are preserved.
TEST_CASE("kear_reconciled_spent: raises spent to popcount when behind", "[game_tables]")
{
    CHECK(mth::tables::kear_reconciled_spent(0b1u, 0) == 1);    // the reported one-kear-on-load case
    CHECK(mth::tables::kear_reconciled_spent(0b1011u, 0) == 3); // 3 collected, 0 spent -> cancel all 3
    CHECK(mth::tables::kear_reconciled_spent(0b1011u, 1) == 3); // partially behind -> catch up
}

TEST_CASE("kear_reconciled_spent: never lowers spent and no-ops when balanced", "[game_tables]")
{
    CHECK(mth::tables::kear_reconciled_spent(0b111u, 3) == 3); // already balanced
    CHECK(mth::tables::kear_reconciled_spent(0b1u, 3) == 3);   // over-spent (would-be negative usable): unchanged
    CHECK(mth::tables::kear_reconciled_spent(0u, 2) == 2);     // no collected bits: spent preserved
}
