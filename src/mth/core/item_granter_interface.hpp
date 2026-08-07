#pragma once

#include <climits>
#include <functional>
#include <utility>

namespace mth
{

// Receipt for a grant with no durable bookkeeping (dev console, one-off effects). Never acked, so it
// can never be mistaken for an AP stream index. Real indices include the negative console-injected
// range, so this has to sit outside that too.
inline constexpr int kNoReceipt = INT_MIN;

// twin: mth/features/item_granter.hpp is the game-coupled impl.
// Grants one item by game itemType. Accepting an item is not applying it: the impl may queue it for a
// later engine window, so acceptance is reported by the return value and APPLICATION by the applied
// sink. Durable "granted" state must key off the sink, never off grant() alone (#175).
class IItemGranter
{
  public:
    virtual ~IItemGranter() = default;

    // receipt: the caller's opaque id for this item, handed back through the applied sink once the
    // grant has actually run. Returns false if unavailable now; caller retries without marking.
    // A true return promises exactly one applied-sink call for the receipt, possibly before returning.
    virtual bool grant(int item_type, int receipt) = 0;

    // Drop anything accepted but not yet applied. Their receipts are never acked, so the caller
    // retries them. Used when the session changes so one seed's queue cannot land in another's save.
    virtual void discard_pending() = 0;

    void set_applied_sink(std::function<void(int)> sink)
    {
        applied_sink_ = std::move(sink);
    }

  protected:
    void notify_applied(int receipt)
    {
        // Receipt first: a kNoReceipt grant can come off the overlay thread, and this must not even
        // read the sink while the game thread may be swapping it.
        if (receipt != kNoReceipt && applied_sink_)
            applied_sink_(receipt);
    }

  private:
    std::function<void(int)> applied_sink_;
};

} // namespace mth
