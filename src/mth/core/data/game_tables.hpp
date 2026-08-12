#pragma once

#include <bit>
#include <cstdint>

namespace mth::tables
{

// Vanilla Pickup::Init legitimately clears the loc_idx the mod's offset self-check reads back: on a spawn
// point carrying the "ForceSmallTreasureCollect" property whose location already reads collected, it pays
// out small bones instead of the item via one 8-byte store, Pickup+0x380 = 0x28ffffffff (loc_idx -1,
// itemType 40 = kItemType_Treasure_Smallest). An AP location hits that on the first room reload after its
// check is sent, so scoring it as drift disabled the redirect for the rest of the session (issue #148).
// Pure so it is unit-testable.
[[nodiscard]] constexpr bool is_small_treasure_collect_rewrite(int stored_loc_idx, int stored_item_type) noexcept
{
    return stored_loc_idx == -1 && stored_item_type == 40;
}

// Reload-durable kear key cancel. usable keys = popcount(SaveSlot+0x1f0) - SaveSlot+0x1f8; under kear_rando
// all kears are AP-controlled, so usable must stay 0. The collect-time spent bump (neutralize_kear_grant) is
// not rebuilt on reload while the collected bitfield is, so spent lags and a free key leaks ("one kear on
// load"). Reconciliation raises spent up to popcount but never lowers it, preserving real lock-spends and an
// already-balanced count. Pure so it is unit-testable.
[[nodiscard]] constexpr int kear_reconciled_spent(std::uint64_t collected_bits, int spent) noexcept
{
    const int popcount = std::popcount(collected_bits);
    return popcount > spent ? popcount : spent;
}

// Resolved s_rItems / s_rItemCollection accessors, shared by the hook modules.
// resolve() is idempotent (each consumer's ctor calls it); every accessor is safe
// while unresolved (returns -1 / 0 / no-ops).
void resolve();

[[nodiscard]] bool collection_resolved();

// storage-kind int of an itemType row; -1 if out of range or unresolved.
[[nodiscard]] int storage_kind(int item_type);

// storage-kind of a location's VANILLA contents (the native reload gate keys on vanilla).
[[nodiscard]] int native_location_kind(int loc_idx);

// Bitfield-only kinds: SetItemCollected is side-effect-free for these (8=key, 12=bonestone, 19=fish).
// Kinds 1/9/11 write a global "have item" bit and are excluded; QueueDestroy handles them instead.
[[nodiscard]] bool is_durable_bit_kind(int kind);

// "Have-item bit" kinds (1=vessel/weapon tiers, 9=subweapon/spell/ability unlocks + capacity pieces,
// 11=trinkets): IsItemCollected keys these on the item's identity/type, not the location's bit-index, so
// owning the item (e.g. an out-of-order AP grant of the vanilla item) reads the location as collected and
// the chest spawns already-open (issue #61). Such AP locations must report the AP checked-state instead.
// The complement of is_durable_bit_kind within the durable families; pure so it is unit-testable.
[[nodiscard]] constexpr bool is_item_keyed_collected_kind(int kind) noexcept
{
    return kind == 1 || kind == 9 || kind == 11;
}

// Decide whether the Items::IsItemCollected override should redirect a query to the AP checked-state.
// It redirects for capacity-upgrade (#8) and item-keyed have-bit (#61) AP locations, with ONE carve-out:
// an ownership query (IsItemCollected param5/b5 = true, i.e. "do I persistently own this item") on a
// weapon-kind (1) location passes through to the real have-item bit. The weapon-swap chest
// (WeaponsChestMenu) enumerates owned weapons via IsItemCollected with b5=true; forcing it to the
// location's AP checked-state hides any weapon received from another player (its location never checked),
// so the swap chest shows no entry. Location-collected queries (b5=false: chest-open, pickup self-kill,
// boss reward-rose) keep the redirect, so #61/#8 are unchanged; kinds 9/11 are untouched (shop #48,
// trinkets). Pure so it is unit-testable.
[[nodiscard]] constexpr bool should_redirect_collected_query(bool is_capacity, int kind, bool ownership_query) noexcept
{
    if (!is_capacity && !is_item_keyed_collected_kind(kind))
        return false; // not an aliasing location
    if (ownership_query && kind == 1)
        return false; // weapon-swap chest ownership read -> real have-item bit
    return true;
}

// The starting weapon the player picked (SaveSlot+0xc60, -1 = none) makes Weapons::GetStarterReplacement
// hand back the whip's collection slot (16) in place of that weapon's tier-3 slot, everywhere the game asks
// - pickups, shops, IsOutOfStock, FillChecklist, KeyBlock, WarpDoor. The apworld pins the two belowdecks
// weapon stands to fixed ids, so after a non-whip start one stand reports a slot no seed contains and the
// other id has no stand left to check it. -1 is the field's "no swap" value (the remap guard is an equality
// test it can never match), so clearing it restores vanilla slot identity; weapon ownership lives in other
// fields and is untouched. Durable save field, hence the bound-save gate. Pure so it is unit-testable.
[[nodiscard]] constexpr bool should_clear_starter_swap(bool authed, bool slot_ok, int current) noexcept
{
    return authed && slot_ok && current != -1;
}

// Armor upgrades (Vitality Vest 0x4f = +25% max HP, Damage armor 0x50) apply their effect DIRECTLY in
// Items::OnPickup (it ORs a bit into SaveSlot+0xc68 before the conditionally-tail-called Items::OnPickupDone)
// - so suppressing OnPickupDone alone leaks the vanilla upgrade for an AP shop buy (issue #71). The mod's
// OnPickup detour suppresses these for AP locations. Pure so it is unit-testable.
[[nodiscard]] constexpr bool is_armor_upgrade_itemtype(int item_type) noexcept
{
    return item_type == 0x4f || item_type == 0x50;
}

// Capacity-upgrade location: vanilla contents itemType in 0x44..0x48 (Magic/Health/Spark/Vial/Trinket
// piece). IsItemCollected for these reads the same SaveSlot bitfield apply_upgrades repurposes as a
// capacity counter, so a per-location collected query is aliased; the mod overrides it (issue #8).
[[nodiscard]] bool is_capacity_upgrade_location(int loc_idx);

// s_rItemCollection row reads (0 / -1 when unresolved or out of range).
[[nodiscard]] std::uint64_t collection_name_key(int idx);
[[nodiscard]] int collection_warp_remap(int idx); // <0 = no remap
[[nodiscard]] std::uint8_t collection_bit_index(int slot);

// Patch s_rItems[kApDummyItemType]: kind 0 (no-op grant) + sprite assets from the donor row.
// Idempotent; best-effort (skipped + logged if s_rItems unresolved or mprotect fails).
void repurpose_dummy_item();

// s_rItemCollection's bit index is a u8 whose 0xff means "this location has no unlock bit"
// (rows 24 and 25 of the shipping table use it). Any other value is a bit position inside a
// SaveSlot u64, so >= 64 without being the sentinel means the field drifted.
inline constexpr std::uint8_t kNoCollectionBit = 0xff;

// Only these may be shifted into the SaveSlot u64. 1ull << 255 is UB and wraps to bit 63 on x86.
[[nodiscard]] constexpr bool collection_bit_usable(std::uint8_t bit)
{
    return bit < 64;
}

// Widens the above by the sentinel: what an intact table can legitimately hold.
[[nodiscard]] constexpr bool collection_bit_plausible(std::uint8_t bit)
{
    return bit < 64 || bit == kNoCollectionBit;
}

// Startup validation for the AP gate. Read-only, and must run before repurpose_dummy_item()
// patches s_rItems[kApDummyItemType], or they validate our own write instead of the game's data.

// Plausible storage kind, and asset pointers that are non-null and reach a NUL-terminated
// printable string. An EMPTY string counts: that is how a row says "no palette".
// repurpose_dummy_item already assumes this shape (it logs the pointers with %s).
[[nodiscard]] bool item_row_shape_ok(int item_type);

// itemType in range and a plausible bit index across the first `sample_count` rows.
[[nodiscard]] bool collection_shape_ok(int sample_count);

} // namespace mth::tables
