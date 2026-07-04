#include "mth/features/goal_tracker.hpp"

#include <bit>

#include "mth/core/ap/ap_state.hpp"
#include "mth/core/data/game_layout.hpp"
#include "mth/core/data/game_symbols.hpp"
#include "mth/core/goal_state.hpp"
#include "mth/core/rando_bridge.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

namespace mth
{

GoalTracker::GoalTracker(RandoBridge &bridge) : bridge_(bridge)
{
    save_manager_ = pal::resolve_game_symbol(sym::save_manager);
    if (save_manager_ == 0)
        pal::logf(pal::LogLevel::Warn, "goal: g_saveManager not resolved; goal tracking disabled");
}

void GoalTracker::evaluate(const ApState &state)
{
    if (save_manager_ == 0)
        return;
    void *slot = pal::active_save_slot(save_manager_);
    if (slot == nullptr)
        return;

    config_ = state.goal_config();
    gens_needed_ = state.goal_generators();
    bosses_needed_ = state.goal_bosses();
    game_cleared_ = *reinterpret_cast<unsigned char *>(static_cast<char *>(slot) + layout::kSaveGameClearOff) != 0;
    gens_done_ = std::popcount(*reinterpret_cast<std::uint64_t *>(static_cast<char *>(slot) + layout::kSaveGeneratorBitsOff));
    bosses_done_ = std::popcount(*reinterpret_cast<std::uint64_t *>(static_cast<char *>(slot) + layout::kSaveBossDefeatedBitsOff));

    if (goal_met(config_, gens_needed_, bosses_needed_, game_cleared_, gens_done_, bosses_done_))
        bridge_.send_goal(); // one-shot + authenticated-gated in the bridge
}

std::vector<std::string> GoalTracker::status_lines() const
{
    return {"goal: config=" + std::to_string(config_) + " cleared=" + std::to_string(game_cleared_) + " generators=" + std::to_string(gens_done_) + "/" +
            std::to_string(gens_needed_) + " bosses=" + std::to_string(bosses_done_) + "/" + std::to_string(bosses_needed_)};
}

} // namespace mth
