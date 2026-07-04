#include "mth/features/boss_hooks.hpp"

#include "mth/core/ap_ids.hpp"
#include "mth/core/game_layout.hpp"
#include "mth/core/game_symbols.hpp"
#include "mth/core/rando_bridge.hpp"
#include "pal/pal_log.hpp"

namespace
{

mth::RandoBridge *g_bridge = nullptr;

void (*g_orig_boss_trigger_death)(void *, void *, unsigned) = nullptr;
void (*g_orig_boss_on_defeated)(void *, void *) = nullptr;

// A boss reached a live-death funnel. Bridge dedups per seed+slot, so bosses that hit both funnels
// in one death (and re-entries) send at most one check.
void boss_defeated_from(void *boss_component, const char *funnel)
{
    if (g_bridge == nullptr)
        return;
    const int boss_index = *reinterpret_cast<unsigned char *>(static_cast<char *>(boss_component) + mth::layout::kBossIndexOff);
    if (!mth::is_boss_index(boss_index))
    {
        pal::logf(pal::LogLevel::Warn, "boss: %s index=%d out of range; offset may have shifted", funnel, boss_index);
        return;
    }
    const int slot = mth::boss_location_slot(boss_index);
    pal::logf(pal::LogLevel::Info, "outbound: boss defeated index=%d -> loc slot=%d (%s)", boss_index, slot, funnel);
    if (g_bridge->is_ap_location(slot))
        g_bridge->on_location_collected(slot);
    else
        pal::logf(pal::LogLevel::Debug, "boss: slot=%d not a valid AP location (apworld may not define this boss)", slot);
}

void repl_boss_trigger_death(void *self, void *params, unsigned a3)
{
    if (g_orig_boss_trigger_death)
        g_orig_boss_trigger_death(self, params, a3);
    boss_defeated_from(self, "TriggerDeathSequence");
}

void repl_boss_on_defeated(void *self, void *reward_info)
{
    if (g_orig_boss_on_defeated)
        g_orig_boss_on_defeated(self, reward_info);
    boss_defeated_from(self, "OnDefeatedNoSkeleton");
}

} // namespace

namespace mth
{

BossHooks::BossHooks(RandoBridge &bridge)
{
    g_bridge = &bridge;
    trigger_death_ = ScopedHook(sym::boss_trigger_death_sequence, reinterpret_cast<void *>(&repl_boss_trigger_death),
                                reinterpret_cast<void **>(&g_orig_boss_trigger_death), "BossComponent::TriggerDeathSequence");
    on_defeated_ = ScopedHook(sym::boss_on_defeated_no_skeleton, reinterpret_cast<void *>(&repl_boss_on_defeated),
                              reinterpret_cast<void **>(&g_orig_boss_on_defeated), "BossComponent::OnDefeatedNoSkeleton");
}

BossHooks::~BossHooks()
{
    // g_bridge nulled before the ScopedHook members remove the detours; the repls null-check it.
    g_bridge = nullptr;
}

} // namespace mth
