#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "mth/features/scene_walk.hpp"

namespace mth
{
class LevelCapHooks;

// twin: mth/core/stat_cap_state.hpp (the cap policy this displays).
// Appends the enforced cap to the three stat panels on the pause screen, so the player can see the
// ceiling without opening the bone-up menu. The game writes those labels only when the menu is built
// or the language changes, so the walk runs on a cadence rather than every tick. Game thread only.
class StatusMenuCaps
{
  public:
    explicit StatusMenuCaps(const LevelCapHooks &caps);

    void on_world_update_end(void *world);
    void on_world_destroy();

  private:
    void annotate(void *status_menu);

    const LevelCapHooks &caps_;
    int ticks_{0};
    bool logged_found_{false};
    SceneWalker walker_{"statuscaps", "the pause panels"};
};

} // namespace mth
