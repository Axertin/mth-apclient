// Capacity-upgrade grants. Every SaveSlot/Player/CombatCore offset below is the same on both builds, and
// the one platform seam (the g_saveManager deref) is already behind pal::active_save_slot.

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "mod/mod_api.hpp"
#include "mth/core/ap/ap_ids.hpp"
#include "mth/core/data/game_symbols.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_mem.hpp"
#include "pal/pal_module.hpp"

// ---- capacity upgrades ----
// Per-upgrade SaveSlot field (index Magic,Health,Spark,Vial,Trinket); popcount = capacity. These
// SaveSlot offsets match on both platforms; UpdateStats recomputes the live maxima from them.
// Build-specific: re-verify against the shipping build. The Vial slot (kVialUpgradeIndex) is a placeholder
// and never written: vials go through the mod API (see App).
namespace
{
constexpr std::ptrdiff_t kUpgradeFieldOff[5] = {0x170, 0x130, 0x54, 0x18c, 0x950};

// Live resource-pool fields, so a capacity grant can keep the missing amount constant instead of
// leaving current untouched (build 9b29bd0d). CombatCore = *(Player+0x130). See the re-note
// 2026-06-24-resource-current-max-fields. Trinket has no depleting pool and is skipped.
constexpr std::ptrdiff_t kCombatCoreOff = 0x130; // Player -> CombatCore*
constexpr std::ptrdiff_t kHpCurOff = 0x1e0;      // CombatCore, float
constexpr std::ptrdiff_t kHpMaxOff = 0x1e8;      // CombatCore, float
// Joules (magic), pinned by the game's own accessors: PlayerGetJoules/PlayerSetJoules use Player+0x117c,
// and UpdateStats writes the max at Player+0x1180. These read 0x1174/0x1178 until r148851; those are the
// backup-sidearm and latched in-flight sidearm itemTypes, so every grant clamped one sidearm itemType
// against the other and the backup slot emptied on the next WriteSave.
constexpr std::ptrdiff_t kMagicCurOff = 0x117c; // Player, int
constexpr std::ptrdiff_t kMagicMaxOff = 0x1180; // Player, int
constexpr std::ptrdiff_t kSparkCurOff = 0x50;   // SaveSlot, int
constexpr std::ptrdiff_t kSparkMaxOff = 0x230;  // CombatCore, int
// Vials are NOT written here: their SaveSlot bitfield offset drifts between builds (#97), so App drives
// them through the offset-free mod-API accessors (mod::set_player_max_vials) instead.

bool g_up_resolved = false;
bool g_up_ok = false;
bool g_up_layout_ok = true; // cleared permanently if an upgrade field reads out of its plausible domain
std::uintptr_t g_up_save_manager = 0;

float fld_f(void *base, std::ptrdiff_t off)
{
    return *reinterpret_cast<float *>(static_cast<char *>(base) + off);
}
int fld_i(void *base, std::ptrdiff_t off)
{
    return *reinterpret_cast<int *>(static_cast<char *>(base) + off);
}
} // namespace

namespace pal
{

bool upgrades_available()
{
    if (g_up_resolved)
        return g_up_ok;
    g_up_resolved = true;
    g_up_save_manager = resolve_game_symbol(mth::sym::save_manager);
    g_up_ok = g_up_save_manager != 0;
    if (!g_up_ok)
        logf(LogLevel::Warn, "upgrades: g_saveManager unresolved; feature disabled");
    return g_up_ok;
}

bool apply_upgrades(const int *counts, void *player)
{
    // Diagnostic (#46): this is called every tick while dirty, so log only when the outcome CHANGES to
    // avoid per-frame spam. Distinguishes which silent guard drops the new-file start-inventory grant.
    static int s_last_outcome = -1;
    auto trace = [&](int outcome, const char *what, void *slot)
    {
        if (outcome == s_last_outcome)
            return;
        s_last_outcome = outcome;
        pal::logf(pal::LogLevel::Debug, "upgrades: apply -> %s (player=%p slot=%p counts=[%d,%d,%d,%d,%d])", what, player, slot, counts[0], counts[1],
                  counts[2], counts[3], counts[4]);
    };
    if (!upgrades_available())
    {
        trace(1, "skip: symbols unavailable", nullptr);
        return false;
    }
    if (player == nullptr)
    {
        trace(2, "skip: player null", nullptr);
        return false;
    }
    if (!g_up_layout_ok)
    {
        trace(3, "skip: layout disabled", nullptr);
        return false;
    }
    // Checked before any write: the owned-bit fields do nothing until UpdateStats recomputes the maxima
    // from them, so without it the grant would be half-applied instead of merely deferred.
    if (!mod::player_stats_api_available())
    {
        trace(6, "skip: PlayerUpdateStats unavailable", nullptr);
        return false;
    }
    void *slot = active_save_slot(g_up_save_manager);
    if (slot == nullptr)
    {
        trace(4, "skip: active SaveSlot* null", slot);
        return false;
    }
    // CombatCore is reached through the Player, so a non-canonical read means the Player is not one (#157
    // faulted on exactly this load, walking a freed Player). Bail rather than treat it as absent: UpdateStats
    // and the magic-pool writes below still go through that same pointer. Leaves the counts dirty, so the
    // next tick retries. A genuinely null CombatCore is the separate case the pool restores already handle.
    void *cc = *reinterpret_cast<void **>(static_cast<char *>(player) + kCombatCoreOff);
    if (cc != nullptr && !pal::pointer_looks_valid(cc))
    {
        trace(5, "skip: CombatCore* invalid", slot);
        return false;
    }

    // Capture the missing amount of each pool before the grant; UpdateStats raises the max but never
    // refills current, so we restore the same missing afterward (new_current = new_max - old_missing).
    // No-op on a resend (max unchanged) and a fresh AP file starts full (missing 0).
    const float hp_missing = cc != nullptr ? fld_f(cc, kHpMaxOff) - fld_f(cc, kHpCurOff) : 0.0f;
    const int magic_missing = fld_i(player, kMagicMaxOff) - fld_i(player, kMagicCurOff);
    const int spark_missing = cc != nullptr ? fld_i(cc, kSparkMaxOff) - fld_i(slot, kSparkCurOff) : 0;

    for (int i = 0; i < 5; ++i)
    {
        if (i == mth::kVialUpgradeIndex)
            continue; // vials are applied via the mod API (offset-free), not this bitfield
        auto &fieldv = *reinterpret_cast<std::uint32_t *>(static_cast<char *>(slot) + kUpgradeFieldOff[i]);
        if (!mth::upgrade_field_in_domain(i, fieldv))
        {
            g_up_layout_ok = false; // upgrades_available()-adjacent guard now short-circuits future calls
            logf(LogLevel::Warn, "upgrades: kUpgradeFieldOff[%d] read=0x%x exceeds cap %d; offset may have shifted, upgrade writes DISABLED", i, fieldv,
                 mth::kUpgradeCaps[i]);
            return false;
        }
        fieldv = mth::upgrade_field_value(i, counts[i], fieldv);
    }
    mod::player_update_stats(); // recompute live maxima from the owned-bit fields; acts on the live player

    if (cc != nullptr)
    {
        const float hp_max = fld_f(cc, kHpMaxOff);
        *reinterpret_cast<float *>(static_cast<char *>(cc) + kHpCurOff) = std::clamp(hp_max - hp_missing, 0.0f, hp_max);
        const int spark_max = fld_i(cc, kSparkMaxOff);
        *reinterpret_cast<int *>(static_cast<char *>(slot) + kSparkCurOff) = std::clamp(spark_max - spark_missing, 0, spark_max);
    }
    const int magic_max = fld_i(player, kMagicMaxOff);
    *reinterpret_cast<int *>(static_cast<char *>(player) + kMagicCurOff) = std::clamp(magic_max - magic_missing, 0, magic_max);

    trace(0, "applied", slot);
    return true;
}

} // namespace pal
