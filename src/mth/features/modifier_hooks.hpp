#pragma once

#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "mth/core/modifier_config.hpp"

namespace mth
{

// Owns the modifier PAL hooks and the enforcement policy. Installs unconditionally when modifiers
// are available; the hooks no-op while disarmed. Armed when a non-empty enforced set exists.
class ModifierHooks
{
  public:
    explicit ModifierHooks(ModifierRequest request);
    ~ModifierHooks();
    ModifierHooks(const ModifierHooks &) = delete;
    ModifierHooks &operator=(const ModifierHooks &) = delete;

    // Console/AP seams (any thread).
    void set_armed(bool on);
    void set_live(int idx, bool on);            // enqueue a live change; applied on the game thread
    void set_enforced(ModifierRequest request); // AP slot_data seam: replace the enforced set
    // Gate for both seed and lockdown; vanilla play (not connected, not test mode) is never affected.
    void set_enforce_live(bool on);
    // True (authed AP session): scope the seed to the AP game's slot, captured on first load.
    void set_ap_scoped(bool on);
    // slot_data seam: add/remove Landing Done from the AP force-on set (Warp Home is always forced).
    void set_ossex_start(bool on);
    void set_cheaper_boneups(bool on);
    // Persistence of the AP-game slot (App bridges this to ApSaveState across sessions): seed a
    // previously-recorded slot so capture is skipped, and read back the slot captured this session.
    void set_ap_slot(int slot);
    [[nodiscard]] int captured_ap_slot() const;
    [[nodiscard]] std::vector<std::string> status_lines() const;

    // Game thread: apply queued live changes (called from App::drive_tick).
    void drain_live();

  private:
    void seed(int slot_index, std::uint32_t words[8]); // PAL seed callback (game thread)
    bool block(int idx) const;                         // PAL lockdown callback (game thread)

    mutable std::mutex mtx_;
    std::set<int> enforced_;
    std::set<int> forced_;
    std::set<int> force_on_{kCheatWarpHome, kCheatCheaperBoneUp, kCheatUnlockBoneUps}; // additive AP force-on (Warp Home always; Landing Done per ossex_start)
    bool armed_{false};
    bool installed_{false};
    int ap_slot_{-1};                                // captured AP-game slot index (-1 = none yet); guarded by mtx_
    std::atomic<bool> enforce_live_{false};          // AP session active (or offline test mode); gates seed + lockdown
    std::atomic<bool> ap_scoped_{false};             // scope the seed to a single (AP) slot vs permissive test mode
    std::vector<std::pair<int, bool>> pending_live_; // (idx, on) queued for the game thread
};

} // namespace mth
