#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace mth
{

class IApLink;
class ApCoordinator;
class AreaReporter;
class RandoBridge;
class BannerQueue;
class ApState;

// Owns the AP network session: link lifetime, the coordinator/area-reporter tick,
// and the net<->hooks RandoBridge. banner_queue_ is the net->render mailbox (overlay only);
// it is owned here and outlives the App-owned overlay_root_ that also references it, because
// ~App tears the overlay down before the managers.
class ApSession
{
  public:
    ApSession(ApState &state, std::function<void()> on_inbound_death);
    ~ApSession();

    ApSession(const ApSession &) = delete;
    ApSession &operator=(const ApSession &) = delete;

    [[nodiscard]] IApLink &link();
    [[nodiscard]] RandoBridge &rando();
#ifdef MTHAP_HAS_OVERLAY
    [[nodiscard]] BannerQueue &banner_queue();
#endif

    void tick(ApState &state, std::optional<std::uint32_t> screen);

  private:
    std::unique_ptr<BannerQueue> banner_queue_; // outlives coordinator_; overlay builds only
    std::unique_ptr<IApLink> link_;
    std::unique_ptr<ApCoordinator> coordinator_;
    std::unique_ptr<AreaReporter> area_reporter_;
    std::unique_ptr<RandoBridge> rando_;
};

} // namespace mth
