#pragma once

#include <cstdint>
#include <set>
#include <vector>

#include "mth/core/ap/ap_ids.hpp"
#include "mth/core/ap/ap_link.hpp"
#include "mth/core/ap/ap_state.hpp"

namespace mth
{

class ApSaveState; // defined in ap_save_state.hpp (same core lib)

// twin: mth/features/{location,boss,goal_tracker}_hooks.hpp drive outbound checks through this.
// Outbound: maps a collected slot to a deduplicated server check. Game-thread-only.
class RandoBridge
{
  public:
    RandoBridge(IApLink &link, ApState &state);

    // Attach durable per-(seed,slot) state on connect; before this, checks are session-only.
    void attach_save_state(ApSaveState &save);

    // Record a collected slot (persisted if attached); send check if connected, else wait for flush().
    void on_location_collected(int collection_slot);

    // Mark a slot checked because the SERVER reported it (Collect / same-slot coop), not the player.
    // Persists into the attached save state but NEVER sends to the server. Returns true if this call
    // newly checked a valid AP location (save attached, not already checked), so the caller can batch a
    // single save() per reconcile pass. No-op returning false when no save is attached.
    [[nodiscard]] bool reconcile_server_checked(int collection_slot);

    // Resend the full checked-set (server dedups). Call on (re)connect.
    void flush();

    // Request scout (item/player) info for the given collection slots; non-AP-location slots are
    // dropped. Results arrive later as an ApScoutInfo event. No-op if none of the slots are AP locations.
    void request_scouts(const std::vector<int> &collection_slots);

    // Send the AP goal (final boss defeated). One-shot per session; no-op unless authenticated.
    void send_goal();

    [[nodiscard]] bool is_ap_location(int collection_slot) const;
    [[nodiscard]] bool is_checked(int collection_slot) const; // false for negative/non-location slots

    // The persisted checked-location slots, or nullptr before a save attaches. Game-thread; used by the
    // native collected-bit enforcement to render server-collected (Collect/coop) chests opened on reload.
    [[nodiscard]] const std::set<int> *checked_slots() const;

  private:
    IApLink &link_;
    ApState &state_;
    ApSaveState *save_{nullptr};
    std::set<std::int64_t> sent_{}; // session fallback used only before save_ attaches
    bool goal_sent_{false};         // one-shot guard for the AP goal
};

} // namespace mth
