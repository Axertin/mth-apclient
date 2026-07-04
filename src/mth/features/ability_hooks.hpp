#pragma once

#include <cstdint>
#include <functional>

#include "mth/core/ability_gate.hpp"
#include "mth/core/ability_ids.hpp"

namespace mth
{

// Drives the PAL ability-gate seam from AP state. The block lambda installed in the ctor runs on the
// game thread (invoked by the detours); enforce_/gate_ are written only from the game-thread per-tick
// path, so plain members suffice (one-frame staleness acceptable).
class AbilityHooks
{
  public:
    explicit AbilityHooks(std::function<bool(std::int64_t item_id)> is_granted);
    ~AbilityHooks();
    AbilityHooks(const AbilityHooks &) = delete;
    AbilityHooks &operator=(const AbilityHooks &) = delete;

    void set_randomized(Ability a, bool on);
    void set_enforce(bool on); // authed && active save slot is the AP slot
    void enforce_train_tick(); // game-thread; force the train-present save byte while Train is blocked

  private:
    std::function<bool(std::int64_t)> is_granted_;
    AbilityGate gate_;
    bool enforce_{false};
    std::uintptr_t g_save_manager_{0};
};

} // namespace mth
