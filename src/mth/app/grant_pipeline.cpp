#include "mth/app/grant_pipeline.hpp"

#include "mth/core/ap/ap_save_state.hpp"
#include "mth/core/ap/ap_state.hpp"
#include "mth/core/inbound_granter.hpp"
#include "mth/features/item_granter.hpp"
#include "mth/features/player_tracker.hpp"

namespace mth
{

GrantPipeline::GrantPipeline(PlayerTracker &tracker, std::function<bool(int)> is_ap_location, std::function<void(int)> on_location_collected)
{
    granter_ = std::make_unique<ItemGranter>(tracker, std::move(is_ap_location), std::move(on_location_collected));
}

GrantPipeline::~GrantPipeline()
{
    inbound_.reset(); // inbound references granter_; tear it down first
    granter_.reset();
}

// Dev-console path: nothing durable to record, so it carries no receipt.
bool GrantPipeline::grant(int item_type)
{
    return granter_->grant(item_type, kNoReceipt);
}

void GrantPipeline::build_inbound(ApState &state, ApSaveState &save_state, std::function<bool()> credit_kear_key)
{
    inbound_.reset(); // release the old save reference before binding the new one
    inbound_ = std::make_unique<InboundGranter>(*granter_, state, save_state, std::move(credit_kear_key));
}

bool GrantPipeline::inbound_ready() const
{
    return inbound_ != nullptr;
}

// The queued grants belong to the save this inbound granter was built against, so they must not
// survive into the next session's save (#175).
void GrantPipeline::release_inbound()
{
    granter_->discard_pending();
    inbound_.reset();
}

void GrantPipeline::tick()
{
    if (inbound_)
        inbound_->tick();
}

void GrantPipeline::drain()
{
    granter_->drain();
}

} // namespace mth
