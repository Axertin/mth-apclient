#pragma once

#include <functional>
#include <memory>

namespace mth
{

class ItemGranter;
class InboundGranter;
class PlayerTracker;
class ApState;
class ApSaveState;

// Owns the received-item grant path: the ItemGranter and its lazily-built InboundGranter.
// upgrades_ (capacity) stays App-owned - it is driven by the newfile-kit suppressor and
// needs App's shared tracker_, so it is not part of this pipeline.
class GrantPipeline
{
  public:
    GrantPipeline(PlayerTracker &tracker, std::function<bool(int)> is_ap_location, std::function<void(int)> on_location_collected);
    ~GrantPipeline();

    GrantPipeline(const GrantPipeline &) = delete;
    GrantPipeline &operator=(const GrantPipeline &) = delete;

    bool grant(int item_type);
    // once, after inbound_ready() is false. credit_kear_key: vanilla-kear-mode key grant effect (#130).
    void build_inbound(ApState &state, ApSaveState &save_state, std::function<bool()> credit_kear_key = {});
    [[nodiscard]] bool inbound_ready() const;
    // Drop the inbound granter; it holds a reference to the ApSaveState, so this must run before that is
    // destroyed. inbound_ready() goes false, so the next session rebuilds it.
    void release_inbound();
    void tick();  // drives InboundGranter
    void drain(); // drives ItemGranter (World::Update pre-hook window)

  private:
    std::unique_ptr<ItemGranter> granter_;
    std::unique_ptr<InboundGranter> inbound_;
};

} // namespace mth
