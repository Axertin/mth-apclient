#include "mth/features/levelcap_hooks.hpp"

#include "mth/core/ap/ap_state.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"

namespace mth
{

LevelCapHooks::LevelCapHooks()
{
    if (!pal::level_cap_available())
    {
        pal::logf(pal::LogLevel::Warn, "levelcap: PAL unavailable; LevelCapHooks inert");
        return;
    }
    pal::set_level_cap_provider([this](int stat, int vanilla) { return provide(stat, vanilla); });
    pal::set_boneup_cap_provider([this](int stat) { return display_cap_for(stat); });
    installed_ = true;
    pal::logf(pal::LogLevel::Info, "levelcap: LevelCapHooks installed");
}

LevelCapHooks::~LevelCapHooks()
{
    enforce_live_.store(false); // stop restricting before the hook is torn down
    if (installed_)
    {
        pal::set_boneup_cap_provider({}); // drop the `this` capture before the hook can fire again
        pal::remove_level_cap_hook();
    }
}

void LevelCapHooks::recompute(const ApState &state)
{
    caps_.recompute(state);
    max_stat_level_ = state.max_stat_level();
}

void LevelCapHooks::set_counts(int attack, int defense, int sidearm)
{
    caps_.set_counts(attack, defense, sidearm);
}

void LevelCapHooks::set_enforce_live(bool on)
{
    enforce_live_.store(on);
}

int LevelCapHooks::provide(int stat, int vanilla_cap)
{
    if (!enforce_live_.load())
        return vanilla_cap; // vanilla play (not connected, not test mode): never restrict
    // Per-stat ceiling (slot_data max for real stats, native for the bone bank), then cap-up gating.
    return caps_.enforced_cap(stat, stat_cap_ceiling(stat, max_stat_level_, vanilla_cap));
}

int LevelCapHooks::display_cap_for(int stat) const
{
    if (!enforce_live_.load() || stat < 0 || stat >= kStatCount)
        return -1;
    return boneup_display_cap(caps_.enforced_cap(stat, max_stat_level_));
}

std::vector<std::string> LevelCapHooks::status_lines() const
{
    std::vector<std::string> out;
    out.push_back("stat caps (granted): attack=" + std::to_string(caps_.granted(0)) + " defense=" + std::to_string(caps_.granted(1)) +
                  " sidearm=" + std::to_string(caps_.granted(2)) + " max_stat_level=" + std::to_string(max_stat_level_));
    return out;
}

} // namespace mth
