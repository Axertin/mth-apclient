#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

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
    std::uintptr_t mod_base_{0};
    std::size_t mod_size_{0};
    bool warned_no_walk_{false};
    bool logged_found_{false};
    std::vector<void *> pending_;
    std::vector<void *> buffer_;
};

} // namespace mth
