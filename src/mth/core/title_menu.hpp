#pragma once

namespace mth
{

// TitleScreen cursor correction while "Start Game" (index 0) is gated off. The game's own wrap only
// fires at the boundaries (raw index 3 or -1); moving Up from index 1 lands on 0 directly with no
// wrap, so a blind "snap to 1" would refuse that motion and freeze the cursor. `previous` is the
// index before the game's update, `current` is after.
[[nodiscard]] int skip_gated_option(int previous, int current) noexcept;

} // namespace mth
