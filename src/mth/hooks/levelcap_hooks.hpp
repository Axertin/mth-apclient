#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "mth/core/stat_cap_state.hpp"

namespace mth
{
class ApState;

// Owns the level-cap PAL hook and the StatCapState policy. Installs unconditionally when the PAL
// reports the feature available; the provider returns vanilla (no restriction) until enforcement is
// live (AP session or offline test mode), so vanilla play is never affected. Game-thread only:
// recompute()/provide() both run on the game thread; only enforce_live_ crosses via atomic.
class LevelCapHooks
{
  public:
    LevelCapHooks();
    ~LevelCapHooks();
    LevelCapHooks(const LevelCapHooks &) = delete;
    LevelCapHooks &operator=(const LevelCapHooks &) = delete;

    void recompute(const ApState &state);                  // refresh caps from received items
    void set_counts(int attack, int defense, int sidearm); // offline test override
    void set_enforce_live(bool on);                        // gate: vanilla play is never restricted

    [[nodiscard]] std::vector<std::string> status_lines() const;

  private:
    int provide(int stat, int vanilla_cap); // PAL callback (game thread)

    StatCapState caps_;
    std::atomic<bool> enforce_live_{false};
    bool installed_{false};
};

} // namespace mth
