#include "mth/app/ap_session.hpp"

#include <utility>
#include <vector>

#include "mth/core/ap/ap_coordinator.hpp"
#include "mth/core/ap/ap_link.hpp"
#include "mth/core/ap/ap_state.hpp"
#include "mth/core/area_reporter.hpp"
#include "mth/core/broadcast.hpp"
#include "mth/core/rando_bridge.hpp"
#include "pal/pal_log.hpp"
#ifdef MTHAP_HAS_NET
#include "mth/net/ap_link_apclient.hpp"
#else
#include "mth/core/ap/null_ap_link.hpp"
#endif

namespace mth
{

ApSession::ApSession(ApState &state, std::function<void()> on_inbound_death, std::function<void(const std::vector<ScoutInfo> &)> on_scout,
                     std::function<void()> on_session_reset)
{
#ifdef MTHAP_HAS_NET
    link_ = std::make_unique<mth::net::ApLink>();
#else
    link_ = std::make_unique<mth::NullApLink>();
    pal::logf(pal::LogLevel::Warn, "net: lane NOT compiled in (NullApLink); connect is a no-op");
#endif
    std::function<void(const std::vector<mth::BannerSegment> &)> on_broadcast;
#ifdef MTHAP_HAS_OVERLAY
    banner_queue_ = std::make_unique<BannerQueue>();
    on_broadcast = [this](const std::vector<mth::BannerSegment> &segs) { banner_queue_->push(segs); };
#endif
    coordinator_ =
        std::make_unique<ApCoordinator>(*link_, state, std::move(on_inbound_death), std::move(on_broadcast), std::move(on_scout), std::move(on_session_reset));
    area_reporter_ = std::make_unique<AreaReporter>(*link_);
    rando_ = std::make_unique<RandoBridge>(*link_, state);
}

ApSession::~ApSession()
{
    // Mirror the App teardown tail: rando -> area_reporter -> coordinator -> link (stops net thread).
    rando_.reset();
    area_reporter_.reset();
    coordinator_.reset();
    link_.reset();
}

IApLink &ApSession::link()
{
    return *link_;
}

RandoBridge &ApSession::rando()
{
    return *rando_;
}

#ifdef MTHAP_HAS_OVERLAY
BannerQueue &ApSession::banner_queue()
{
    return *banner_queue_;
}
#endif

void ApSession::tick(ApState &state, std::optional<std::uint32_t> screen)
{
    coordinator_->tick();
    area_reporter_->tick(state.authenticated(), screen);
}

} // namespace mth
