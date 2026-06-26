#pragma once

#include <bit>
#include <cstdint>

namespace mth::tables
{

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

} // namespace mth::tables
