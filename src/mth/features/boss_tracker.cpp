#include "mth/features/boss_tracker.hpp"

#include <bit>

#include "mod/mod_api.hpp"
#include "mth/core/ap/ap_ids.hpp"
#include "mth/core/data/game_symbols.hpp"
#include "mth/core/rando_bridge.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

namespace mth
{

BossTracker::BossTracker(RandoBridge &bridge) : bridge_(bridge)
{
    save_manager_ = pal::resolve_game_symbol(sym::save_manager);
    if (save_manager_ == 0)
        pal::logf(pal::LogLevel::Warn, "boss: g_saveManager not resolved; boss-defeat checks disabled");
}

void BossTracker::prime(const void *slot, const char *why, std::uint64_t mask)
{
    prev_slot_ = slot;
    prev_mask_ = mask;
    primed_ = true;
    pal::logf(pal::LogLevel::Debug, "boss: baseline primed (%s) slot=%p mask=0x%llx", why, slot, static_cast<unsigned long long>(mask));
}

void BossTracker::poll()
{
    if (save_manager_ == 0)
        return;
    // PlayerGetBossesDefeated reads *(g_saveManager+0x18)+0x280 with no null guard, so it faults
    // wherever no slot is bound. No gameplay happens without a slot, so dropping the baseline here
    // loses no kill: it just re-primes on the next bind.
    const void *slot = pal::active_save_slot(save_manager_);
    if (slot == nullptr)
    {
        primed_ = false;
        return;
    }

    const std::uint64_t mask = mod::player_bosses_defeated();
    if (!primed_)
    {
        prime(slot, "slot bound", mask);
        return;
    }
    // The title screen binds its own slot before the run's, so a baseline taken against one slot must
    // never be diffed against another - that reported a fresh save's pre-set bits as kills.
    if (slot != prev_slot_)
    {
        prime(slot, "slot changed", mask);
        return;
    }
    if (mask == prev_mask_)
        return;
    // Normal play only ORs bits in. Losing one means the slot's contents were replaced under the same
    // pointer (takeover write, NG+ clear), so re-baseline rather than diff across it.
    if ((prev_mask_ & ~mask) != 0)
    {
        prime(slot, "mask lost bits; save changed", mask);
        return;
    }

    const std::uint64_t risen = mask & ~prev_mask_;
    // No tick legitimately kills three bosses. A rise that wide is save data arriving under an
    // already-primed slot, not gameplay.
    if (std::popcount(risen) >= 3)
    {
        prime(slot, "wide rise; save data loaded", mask);
        return;
    }
    prev_mask_ = mask;
    for (int idx = 0; idx <= kMaxBossIndex; ++idx)
    {
        if ((risen & (1ULL << idx)) == 0)
            continue;
        const int slot = boss_location_slot(idx);
        pal::logf(pal::LogLevel::Info, "outbound: boss defeated index=%d -> loc slot=%d (defeat bit)", idx, slot);
        if (bridge_.is_ap_location(slot))
            bridge_.on_location_collected(slot);
        else
            pal::logf(pal::LogLevel::Debug, "boss: slot=%d not a valid AP location (apworld may not define this boss)", slot);
    }
    // Bits above kMaxBossIndex are not boss indices; log once so a layout shift is visible.
    const std::uint64_t stray = risen & ~((kMaxBossIndex >= 63) ? ~0ULL : ((1ULL << (kMaxBossIndex + 1)) - 1));
    if (stray != 0)
        pal::logf(pal::LogLevel::Warn, "boss: defeat bits set outside the boss index range: 0x%llx", static_cast<unsigned long long>(stray));
}

} // namespace mth
