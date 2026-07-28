#include "mth/features/title_gate.hpp"

#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"

namespace mth
{

TitleGate::TitleGate(std::function<bool()> connected, std::function<bool()> claim_start)
{
    // Copied, not moved: the cursor hook below takes it too.
    auto suppress = [connected, claim_start = std::move(claim_start)]()
    {
        if (!connected())
            return true;
        // Claimed launches must let the vanilla StartGame run; the substate it requests is what builds
        // the profile-select menu. An unclaimed one blocks rather than reaching a vanilla save.
        return !(claim_start && claim_start());
    };
    auto gate = [connected = std::move(connected)]()
    {
        const bool up = connected();
        pal::set_title_start_option_text(up ? "" : "Disconnected"); // empty restores the localized label
        return up;
    };
    if (!pal::install_title_gate_hook(std::move(gate)))
        pal::logf(pal::LogLevel::Warn, "title: gating inactive; Start Game stays selectable");
    if (!pal::install_start_game_suppress_hook(std::move(suppress)))
        pal::logf(pal::LogLevel::Warn, "title: start-game backstop inactive; Start Game not blocked if reached");
}

TitleGate::~TitleGate()
{
    pal::remove_start_game_suppress_hook();
    pal::remove_title_gate_hook();
}

} // namespace mth
