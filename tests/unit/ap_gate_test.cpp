#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/ap_gate.hpp"

namespace
{

// All static checks passing, liveness not yet observed, well inside the timeout.
mth::GateInputs healthy_pending()
{
    mth::GateInputs in;
    in.mod_api_present = true;
    in.symbols_resolved = true;
    in.item_table_shape_ok = true;
    in.layout_probes_ok = true;
    in.revision_known = true;
    in.mod_api_shape_ok = true;
    in.worldupdate_observed = false;
    in.ticks_since_probe_installed = 1;
    return in;
}

mth::GateInputs healthy_clear()
{
    mth::GateInputs in = healthy_pending();
    in.worldupdate_observed = true;
    return in;
}

} // namespace

TEST_CASE("gate: all inputs good and liveness observed -> Clear", "[gate]")
{
    REQUIRE(mth::evaluate(healthy_clear()) == mth::GateVerdict::Clear);
    REQUIRE(mth::refusal_reason(healthy_clear()).empty());
}

TEST_CASE("gate: any single static check failing -> Refused", "[gate]")
{
    // Each static input is independently fatal: the design is all-or-nothing, so there is no
    // combination where one failing check still permits AP behavior.
    {
        mth::GateInputs in = healthy_clear();
        in.mod_api_present = false;
        REQUIRE(mth::evaluate(in) == mth::GateVerdict::Refused);
    }
    {
        mth::GateInputs in = healthy_clear();
        in.symbols_resolved = false;
        REQUIRE(mth::evaluate(in) == mth::GateVerdict::Refused);
    }
    {
        mth::GateInputs in = healthy_clear();
        in.item_table_shape_ok = false;
        REQUIRE(mth::evaluate(in) == mth::GateVerdict::Refused);
    }
    {
        mth::GateInputs in = healthy_clear();
        in.layout_probes_ok = false;
        REQUIRE(mth::evaluate(in) == mth::GateVerdict::Refused);
    }
}

TEST_CASE("gate: static checks pass but liveness pending -> Pending until the timeout", "[gate]")
{
    mth::GateInputs in = healthy_pending();
    REQUIRE(mth::evaluate(in) == mth::GateVerdict::Pending);

    in.ticks_since_probe_installed = mth::kLivenessTimeoutTicks - 1;
    REQUIRE(mth::evaluate(in) == mth::GateVerdict::Pending);

    in.ticks_since_probe_installed = mth::kLivenessTimeoutTicks;
    REQUIRE(mth::evaluate(in) == mth::GateVerdict::Refused);
}

TEST_CASE("gate: liveness observed beats the timeout", "[gate]")
{
    // A slow load must not refuse a build where the hook does fire.
    mth::GateInputs in = healthy_clear();
    in.ticks_since_probe_installed = mth::kLivenessTimeoutTicks * 10;
    REQUIRE(mth::evaluate(in) == mth::GateVerdict::Clear);
}

TEST_CASE("gate: an unknown revision alone does not refuse", "[gate]")
{
    // Revision is a trigger to verify, not a verdict: gating on "unfamiliar build" would brick
    // the mod on every game patch, including ones where nothing load-bearing moved.
    mth::GateInputs in = healthy_clear();
    in.revision_known = false;
    REQUIRE(mth::evaluate(in) == mth::GateVerdict::Clear);
    REQUIRE(mth::refusal_reason(in).empty());
}

TEST_CASE("gate: refusal_reason is non-empty exactly when refused, and distinct per cause", "[gate]")
{
    std::vector<mth::GateInputs> refused;
    {
        mth::GateInputs in = healthy_clear();
        in.mod_api_present = false;
        refused.push_back(in);
    }
    {
        mth::GateInputs in = healthy_clear();
        in.symbols_resolved = false;
        refused.push_back(in);
    }
    {
        mth::GateInputs in = healthy_clear();
        in.item_table_shape_ok = false;
        refused.push_back(in);
    }
    {
        mth::GateInputs in = healthy_clear();
        in.layout_probes_ok = false;
        refused.push_back(in);
    }
    {
        mth::GateInputs in = healthy_pending();
        in.ticks_since_probe_installed = mth::kLivenessTimeoutTicks;
        refused.push_back(in);
    }

    std::vector<std::string> reasons;
    for (const mth::GateInputs &in : refused)
    {
        REQUIRE(mth::evaluate(in) == mth::GateVerdict::Refused);
        reasons.push_back(mth::refusal_reason(in));
        REQUIRE_FALSE(reasons.back().empty());
    }
    REQUIRE(mth::refusal_reason(healthy_clear()).empty());
    REQUIRE(mth::refusal_reason(healthy_pending()).empty());

    // Five causes, five reasons. A banner that reads the same for a stale signature as for a missing
    // mod API costs the player the one thing the banner is for.
    for (std::size_t i = 0; i < reasons.size(); ++i)
        for (std::size_t j = i + 1; j < reasons.size(); ++j)
        {
            INFO(reasons[i] << " / " << reasons[j]);
            REQUIRE(reasons[i] != reasons[j]);
        }

    // A missing mod API also explains a failed symbol resolve, so it is the one reported.
    mth::GateInputs both = healthy_clear();
    both.mod_api_present = false;
    both.symbols_resolved = false;
    mth::GateInputs api_only = healthy_clear();
    api_only.mod_api_present = false;
    REQUIRE(mth::refusal_reason(both) == mth::refusal_reason(api_only));
}

TEST_CASE("gate latch: Clear is terminal", "[gate]")
{
    mth::GateLatch latch;
    REQUIRE(latch.verdict() == mth::GateVerdict::Pending);
    REQUIRE_FALSE(latch.settled());

    REQUIRE(latch.update(healthy_clear()) == mth::GateVerdict::Clear);
    REQUIRE(latch.settled());

    // A later input that would evaluate to Refused must not revoke a granted Clear: features
    // are already installed and a run may already be in progress.
    mth::GateInputs broken = healthy_clear();
    broken.symbols_resolved = false;
    REQUIRE(latch.update(broken) == mth::GateVerdict::Clear);
    REQUIRE(latch.verdict() == mth::GateVerdict::Clear);
}

TEST_CASE("gate latch: Refused is terminal", "[gate]")
{
    mth::GateLatch latch;
    mth::GateInputs broken = healthy_clear();
    broken.layout_probes_ok = false;

    REQUIRE(latch.update(broken) == mth::GateVerdict::Refused);
    REQUIRE(latch.settled());

    // No retry: a refusal holds for the process lifetime.
    REQUIRE(latch.update(healthy_clear()) == mth::GateVerdict::Refused);
    REQUIRE(latch.verdict() == mth::GateVerdict::Refused);
}

TEST_CASE("gate latch: stays Pending across repeated updates until an input settles it", "[gate]")
{
    // Pending is the one verdict the latch re-evaluates, so a second update is all it takes to show
    // it does not stick. The timeout boundary itself belongs to the evaluate() case above.
    mth::GateLatch latch;
    mth::GateInputs in = healthy_pending();
    REQUIRE(latch.update(in) == mth::GateVerdict::Pending);
    REQUIRE(latch.update(in) == mth::GateVerdict::Pending);
    REQUIRE_FALSE(latch.settled());

    in.worldupdate_observed = true;
    REQUIRE(latch.update(in) == mth::GateVerdict::Clear);
}

TEST_CASE("gate: mod_api_shape_ok is informational and never moves the verdict", "[gate]")
{
    mth::GateInputs in = healthy_clear();
    in.mod_api_shape_ok = false;
    REQUIRE(mth::evaluate(in) == mth::GateVerdict::Clear);
    REQUIRE(mth::refusal_reason(in).empty());

    mth::GateInputs pending = healthy_pending();
    pending.mod_api_shape_ok = false;
    REQUIRE(mth::evaluate(pending) == mth::GateVerdict::Pending);
}

TEST_CASE("gate: connect is refused only when enforcing and refused", "[gate]")
{
    REQUIRE(mth::should_refuse_connect(true, mth::GateVerdict::Refused));
    REQUIRE_FALSE(mth::should_refuse_connect(true, mth::GateVerdict::Clear));
    REQUIRE_FALSE(mth::should_refuse_connect(false, mth::GateVerdict::Refused));
    REQUIRE_FALSE(mth::should_refuse_connect(false, mth::GateVerdict::Clear));
}

TEST_CASE("gate: Pending does not block connect", "[gate]")
{
    // The liveness proof cannot arrive before the game runs, so holding connects until it does
    // would strand a launch-time login behind a 600-tick wait for no diagnostic gain.
    REQUIRE_FALSE(mth::should_refuse_connect(true, mth::GateVerdict::Pending));
    REQUIRE(mth::evaluate(healthy_pending()) == mth::GateVerdict::Pending);
}
