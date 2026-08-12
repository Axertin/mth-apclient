#include <catch2/catch_test_macros.hpp>

#include "mth/core/data/game_tables.hpp"

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

// is_small_treasure_collect_rewrite pins the exact loc_idx/itemType pair vanilla Pickup::Init stores over an
// already-collected small treasure, so the Pickup offset self-check does not score it as layout drift (#148).
TEST_CASE("is_small_treasure_collect_rewrite: the vanilla collected-treasure pair is not drift", "[game_tables]")
{
    CHECK(mth::tables::is_small_treasure_collect_rewrite(-1, 40)); // Pickup+0x380 = 0x28ffffffff
}

TEST_CASE("is_small_treasure_collect_rewrite: anything else is real drift", "[game_tables]")
{
    CHECK_FALSE(mth::tables::is_small_treasure_collect_rewrite(-1, 51));  // loc cleared, but not the bones payout
    CHECK_FALSE(mth::tables::is_small_treasure_collect_rewrite(172, 40)); // bones itemType, but loc not cleared
}

// should_clear_starter_swap decides whether the per-tick starter-weapon-swap clear runs. The chosen
// starter weapon makes Weapons::GetStarterReplacement remap its tier-3 collection slot to the whip's (16),
// so a boat-hold chest reports a slot no seed contains and its sibling location becomes uncollectable.
// -1 is the field's "no swap" value, so clearing it restores vanilla slot identity. It is a durable save
// field, so it is only written to the bound AP save (the AP-slot check is false when not authenticated).
TEST_CASE("should_clear_starter_swap: a swapped bound AP save is cleared", "[game_tables]")
{
    CHECK(mth::tables::should_clear_starter_swap(/*authed=*/true, /*slot_ok=*/true, /*current=*/2)); // hammer start
    CHECK(mth::tables::should_clear_starter_swap(true, true, 0));                                    // whip start still carries a type
}

TEST_CASE("should_clear_starter_swap: an unswapped save is left alone", "[game_tables]")
{
    CHECK_FALSE(mth::tables::should_clear_starter_swap(/*authed=*/true, /*slot_ok=*/true, /*current=*/-1)); // already vanilla
}

TEST_CASE("should_clear_starter_swap: never writes outside the bound AP save", "[game_tables]")
{
    CHECK_FALSE(mth::tables::should_clear_starter_swap(/*authed=*/false, /*slot_ok=*/true, /*current=*/2)); // slot_ok is true when offline
    CHECK_FALSE(mth::tables::should_clear_starter_swap(true, false, 2));                                    // authed, but a different save
    CHECK_FALSE(mth::tables::should_clear_starter_swap(false, false, 2));
}

// SaveSlot+0xc38[fam] is the companion of the +0xc24[fam] ownership bitfield: a BIT INDEX into it, not a
// power level (the game revokes a weapon with `0xc24[fam] &= ~(1 << (0xc38[fam] & 0x1f))`). The top
// authorized bit is what the game's own ordered grants leave there.
TEST_CASE("weapon_active_bit: the companion field is a bit index into the ownership mask", "[game_tables]")
{
    CHECK(mth::tables::weapon_active_bit(0b001u) == 0); // one tier granted
    CHECK(mth::tables::weapon_active_bit(0b011u) == 1);
    CHECK(mth::tables::weapon_active_bit(0b111u) == 2);
    CHECK(mth::tables::weapon_active_bit(0b100u) == 2); // the intro's own grant: one weapon, bit 2
    CHECK(mth::tables::weapon_active_bit(0u) == 0);     // owns nothing, as a fresh save reads
}

// The clamp is destructive, so it refuses the one shape it cannot tell apart from a real revoke: an empty
// authorized mask over a save that owns weapons is what a receipt list that has not loaded yet looks like,
// and clamping then would delete the player's weapons permanently.
TEST_CASE("weapon_clamp_ready: refuses to revoke everything on an empty receipt list", "[game_tables]")
{
    CHECK_FALSE(mth::tables::weapon_clamp_ready(/*authorized_any=*/0u, /*owned_any=*/0b100u));
    CHECK(mth::tables::weapon_clamp_ready(0b001u, 0b100u)); // AP granted something: a real revoke
    CHECK(mth::tables::weapon_clamp_ready(0u, 0u));         // nothing granted, nothing owned: consistent
}

// The clamp masks and rewrites both weapon fields every tick, so a shifted offset would grind unrelated save
// state down rather than fail outright. Three tiers exist and the companion field is a bit index into them,
// so anything wider than that is not the pair we think we are looking at.
TEST_CASE("weapon_fields_in_domain: a read wider than three tiers is not the weapon pair", "[game_tables]")
{
    CHECK(mth::tables::weapon_fields_in_domain(/*owned=*/0b000u, /*active=*/0)); // fresh save
    CHECK(mth::tables::weapon_fields_in_domain(0b100u, 2));                      // the intro's own grant
    CHECK(mth::tables::weapon_fields_in_domain(0b111u, 2));                      // every tier owned
    CHECK_FALSE(mth::tables::weapon_fields_in_domain(0b1000u, 0));               // a fourth tier does not exist
    CHECK_FALSE(mth::tables::weapon_fields_in_domain(0xffffffffu, 0));
    CHECK_FALSE(mth::tables::weapon_fields_in_domain(0b001u, 3));  // a bit index, not a count of owned tiers
    CHECK_FALSE(mth::tables::weapon_fields_in_domain(0b001u, -1)); // negative shifts the game's revoke UB
}

TEST_CASE("collection bit index: 0xff is the table's no-bit sentinel, not drift", "[tables][gate]")
{
    // Measured against the shipping table: rows 24 and 25 carry 0xff, meaning "this location has
    // no unlock bit". Treating that as corruption made the AP gate refuse a known-good build.
    REQUIRE(mth::tables::collection_bit_plausible(0));
    REQUIRE(mth::tables::collection_bit_plausible(1));
    REQUIRE(mth::tables::collection_bit_plausible(63));
    REQUIRE(mth::tables::collection_bit_plausible(mth::tables::kNoCollectionBit));

    // Anything else at or above 64 cannot be a bit position in a u64 field.
    REQUIRE_FALSE(mth::tables::collection_bit_plausible(64));
    REQUIRE_FALSE(mth::tables::collection_bit_plausible(100));
    REQUIRE_FALSE(mth::tables::collection_bit_plausible(254));
}

TEST_CASE("collection bit index: only < 64 may be shifted", "[tables][gate]")
{
    // seed_removed_locks does `1ull << bit`. On x86 a shift by 255 wraps to bit 63 and silently
    // unlocks an unrelated lock in the player's save, so the sentinel must never reach the shift.
    REQUIRE(mth::tables::collection_bit_usable(0));
    REQUIRE(mth::tables::collection_bit_usable(63));
    REQUIRE_FALSE(mth::tables::collection_bit_usable(64));
    REQUIRE_FALSE(mth::tables::collection_bit_usable(mth::tables::kNoCollectionBit));
}
