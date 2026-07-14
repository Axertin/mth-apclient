#pragma once

#include <optional>

namespace mth
{
class ApState;

// slot_data "wallet_cap": the bone wallet is capped by the number of received wallet items
// (kProgWalletId). Base 750, +500 per item, and uncapped once kWalletFreeAt items are held.
inline constexpr int kWalletBase = 750;
inline constexpr int kWalletStep = 500;
inline constexpr int kWalletFreeAt = 8;

// Pure per-item cap: nullopt once count >= kWalletFreeAt (no cap), else kWalletBase + kWalletStep*count.
// Unit-testable, no platform deps.
[[nodiscard]] constexpr std::optional<int> wallet_cap_for(int count) noexcept
{
    if (count >= kWalletFreeAt)
        return std::nullopt;
    if (count < 0)
        count = 0;
    return kWalletBase + kWalletStep * count;
}

// twin: App::enforce_wallet_cap clamps live bones to this each frame.
// Derived state from received AP wallet items. Pure logic, no platform deps.
class WalletCapState
{
  public:
    // Recount received wallet items from the AP received-items list. Idempotent; safe every tick.
    void recompute(const ApState &state);

    // Offline test seam: set the received count directly (bypasses AP).
    void set_count(int count);

    // The bone cap to enforce, or nullopt for "uncapped".
    [[nodiscard]] std::optional<int> enforced_cap() const;

    // Raw received count (diagnostics).
    [[nodiscard]] int received() const
    {
        return count_;
    }

  private:
    int count_{0};
};

} // namespace mth
