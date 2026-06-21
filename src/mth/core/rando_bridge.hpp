#pragma once

#include <cstdint>
#include <set>

#include "mth/core/ap_ids.hpp"
#include "mth/core/ap_link.hpp"
#include "mth/core/ap_state.hpp"

namespace mth
{

class ApSaveState; // defined in ap_save_state.hpp (same core lib)

// Outbound: maps a collected slot to a deduplicated server check. Game-thread-only.
class RandoBridge
{
  public:
    RandoBridge(IApLink &link, ApState &state);

    // Attach durable per-(seed,slot) state on connect; before this, checks are session-only.
    void attach_save_state(ApSaveState &save);

    // Record a collected slot (persisted if attached); send check if connected, else wait for flush().
    void on_location_collected(int collection_slot);

    // Resend the full checked-set (server dedups). Call on (re)connect.
    void flush();

    // Send the AP goal (final boss defeated). One-shot per session; no-op unless authenticated.
    void send_goal();

    [[nodiscard]] bool is_ap_location(int collection_slot) const;
    [[nodiscard]] bool is_checked(int collection_slot) const; // false for negative/non-location slots

  private:
    IApLink &link_;
    ApState &state_;
    ApSaveState *save_{nullptr};
    std::set<std::int64_t> sent_{}; // session fallback used only before save_ attaches
    bool goal_sent_{false};         // one-shot guard for the AP goal
};

} // namespace mth
