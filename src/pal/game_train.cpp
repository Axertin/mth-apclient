// SaveSlot enforcement that reaches the slot through pal::active_save_slot, so the one platform-divergent
// read stays behind that seam and these offsets are identical on both builds. The train-presence and
// boarding writes stay in the platform files: they share their offsets with the TrainAuthority detour,
// which reads a per-platform owner offset.

#include <bit>
#include <cstddef>
#include <cstdint>

#include "mth/core/ap/ap_ids.hpp"
#include "mth/core/data/game_layout.hpp"
#include "mth/core/data/game_tables.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"

namespace
{

// SaveSlot unlocked-train-lines bitfield (5 low bits, one per destination). Set by the TrainAuthority ctor
// on station footfall; the AP client clamps it to the granted-ticket mask. Same layout on both platforms.
constexpr std::ptrdiff_t kSaveTrainUnlockedLinesOff = 0x1e0;
// SaveSlot donation-machine progress (u32 uTicketProgress). Read fresh by the machine every tick and never
// reset, with exactly three readers (the state machine's goal compare, the progress bar, the deposit dial),
// so raising it is a complete way to lower the donation cost. Same layout on both platforms.
constexpr std::ptrdiff_t kSaveTicketProgressOff = 0x1bc;

} // namespace

namespace pal
{

void enforce_train_destinations(std::uintptr_t save_manager_global, std::uint32_t line_mask)
{
    void *slot = active_save_slot(save_manager_global);
    if (slot == nullptr)
        return;
    // Unlocked-lines bitfield is a byte at +0x1e0 (5 low bits); the footfall unlock only ORs bits in, so
    // writing the granted mask each frame clears any line the game auto-unlocked on a station visit.
    *reinterpret_cast<std::uint8_t *>(static_cast<char *>(slot) + kSaveTrainUnlockedLinesOff) = static_cast<std::uint8_t>(line_mask & 0xffu);
}

void clear_starter_weapon_swap(std::uintptr_t save_manager_global, bool authed, bool slot_ok)
{
    static bool logged = false;
    void *slot = active_save_slot(save_manager_global);
    if (slot == nullptr)
        return;
    auto &type = *reinterpret_cast<int *>(static_cast<char *>(slot) + mth::layout::kSaveStarterWeaponTypeOff);
    if (!mth::tables::should_clear_starter_swap(authed, slot_ok, type))
        return;
    const int prev = type;
    type = -1;
    // A save load re-seeds the field, so this can fire more than once per session; only the first is Info.
    const LogLevel level = logged ? LogLevel::Debug : LogLevel::Info;
    logged = true;
    logf(level, "starter: cleared weapon swap (was type=%d); belowdecks weapon stands restored to vanilla slots", prev);
}

void enforce_weapon_ownership(std::uintptr_t save_manager_global, const std::uint32_t *authorized, bool authed, bool slot_ok)
{
    static bool logged[mth::kWeaponFamilyCount] = {}; // per family: Info on the first correction, Debug on repeats
    static bool warned = false;
    static bool layout_ok = true;
    if (!layout_ok)
        return; // an implausible read already disabled the clamp for the rest of the process
    if (!authed || !slot_ok || authorized == nullptr)
        return; // durable and destructive: bound AP save only (slot_ok alone is true while offline)
    void *slot = active_save_slot(save_manager_global);
    if (slot == nullptr)
        return;
    auto *owned = reinterpret_cast<std::uint32_t *>(static_cast<char *>(slot) + mth::layout::kSaveWeaponOwnedBitsOff);
    auto *active = reinterpret_cast<int *>(static_cast<char *>(slot) + mth::layout::kSaveWeaponActiveTierOff);

    std::uint32_t authorized_any = 0;
    std::uint32_t owned_any = 0;
    for (int fam = 0; fam < mth::kWeaponFamilyCount; ++fam)
    {
        // Every family is checked before the first write: the clamp rewrites all 40 bytes of the pair each tick (see weapon_fields_in_domain).
        if (!mth::tables::weapon_fields_in_domain(owned[fam], active[fam]))
        {
            layout_ok = false;
            logf(LogLevel::Error,
                 "weapons: family=%d owned=0x%x tier=%d outside the tier domain (mask 0x%x, tier 0..%d); offsets may have shifted, weapon writes DISABLED", fam,
                 owned[fam], active[fam], mth::layout::kWeaponTierBits, std::bit_width(mth::layout::kWeaponTierBits) - 1);
            return;
        }
        authorized_any |= authorized[fam];
        owned_any |= owned[fam] & mth::layout::kWeaponTierBits;
    }
    if (!mth::tables::weapon_clamp_ready(authorized_any, owned_any))
    {
        if (!warned)
            logf(LogLevel::Warn, "weapons: save owns 0x%x but AP has granted nothing yet; ownership clamp skipped until the receipts load", owned_any);
        warned = true;
        return;
    }
    warned = false;

    for (int fam = 0; fam < mth::kWeaponFamilyCount; ++fam)
    {
        const std::uint32_t cur = owned[fam];
        const std::uint32_t want = authorized[fam];
        const int want_tier = mth::tables::weapon_active_bit(want);
        if ((cur & mth::layout::kWeaponTierBits) == want && active[fam] == want_tier)
            continue; // the common case is a pure read
        const LogLevel level = logged[fam] ? LogLevel::Debug : LogLevel::Info;
        logged[fam] = true;
        logf(level, "weapons: family=%d owned 0x%x -> 0x%x, tier %d -> %d (clamped to the AP grants)", fam, cur & mth::layout::kWeaponTierBits, want,
             active[fam], want_tier);
        owned[fam] = (cur & ~mth::layout::kWeaponTierBits) | want; // anything outside the three tier bits is not ours
        active[fam] = want_tier;
    }
}

void seed_ticket_machine_progress(std::uintptr_t save_manager_global, std::uint32_t seed)
{
    if (seed == 0)
        return;
    void *slot = active_save_slot(save_manager_global);
    if (slot == nullptr)
        return;
    // Raise only: a player already past the seed keeps their own progress, and re-running this each tick
    // must not undo a deposit in flight.
    auto *progress = reinterpret_cast<std::uint32_t *>(static_cast<char *>(slot) + kSaveTicketProgressOff);
    if (*progress < seed)
        *progress = seed;
}

} // namespace pal
