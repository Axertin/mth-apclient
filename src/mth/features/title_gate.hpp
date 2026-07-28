#pragma once

#include <functional>

namespace mth
{

// Keeps the title-screen cursor off "Start Game" while AP is disconnected. The game has no
// enabled/disabled concept for menu options, so the gate is the cursor position itself.
// TitleScreen::UpdateState writes the cursor and dispatches the confirm in one call, so a cursor
// correction alone cannot stop StartGame from having already run; StartGame is suppressed directly
// as the real backstop. Installs both hooks on construction, removes both on destruction.
class TitleGate
{
  public:
    // `claim_start` runs on a connected StartGame confirm and returns whether something took
    // ownership of the launch. False blocks it: never fall through to the vanilla profile-select.
    TitleGate(std::function<bool()> connected, std::function<bool()> claim_start);
    ~TitleGate();

    TitleGate(const TitleGate &) = delete;
    TitleGate &operator=(const TitleGate &) = delete;
};

} // namespace mth
