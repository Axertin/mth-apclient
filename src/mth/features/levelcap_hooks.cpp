#include "mth/features/levelcap_hooks.hpp"

#include "mth/core/ap_state.hpp"
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
    installed_ = true;
    pal::logf(pal::LogLevel::Info, "levelcap: LevelCapHooks installed");
}

LevelCapHooks::~LevelCapHooks()
{
    enforce_live_.store(false); // stop restricting before the hook is torn down
    if (installed_)
        pal::remove_level_cap_hook();
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

std::vector<std::string> LevelCapHooks::status_lines() const
{
    std::vector<std::string> out;
    out.push_back("stat caps (granted): attack=" + std::to_string(caps_.granted(0)) + " defense=" + std::to_string(caps_.granted(1)) +
                  " sidearm=" + std::to_string(caps_.granted(2)) + " max_stat_level=" + std::to_string(max_stat_level_));
    return out;
}

} // namespace mth
