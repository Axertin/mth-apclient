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

TEST_CASE("gate: refusal_reason names the first failing input", "[gate]")
{
    {
        mth::GateInputs in = healthy_clear();
        in.mod_api_present = false;
        in.symbols_resolved = false; // both broken; the more fundamental one is reported
        REQUIRE(mth::refusal_reason(in).find("mod API") != std::string::npos);
    }
    {
        mth::GateInputs in = healthy_clear();
        in.symbols_resolved = false;
        REQUIRE(mth::refusal_reason(in).find("game functions") != std::string::npos);
    }
    {
        mth::GateInputs in = healthy_clear();
        in.item_table_shape_ok = false;
        REQUIRE(mth::refusal_reason(in).find("item table") != std::string::npos);
    }
    {
        mth::GateInputs in = healthy_clear();
        in.layout_probes_ok = false;
        REQUIRE(mth::refusal_reason(in).find("layout probe") != std::string::npos);
    }
    {
        mth::GateInputs in = healthy_pending();
        in.ticks_since_probe_installed = mth::kLivenessTimeoutTicks;
        REQUIRE(mth::refusal_reason(in).find("WorldUpdate") != std::string::npos);
    }
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
    mth::GateLatch latch;
    mth::GateInputs in = healthy_pending();
    for (int tick = 1; tick < mth::kLivenessTimeoutTicks; ++tick)
    {
        in.ticks_since_probe_installed = tick;
        REQUIRE(latch.update(in) == mth::GateVerdict::Pending);
    }
    in.worldupdate_observed = true;
    REQUIRE(latch.update(in) == mth::GateVerdict::Clear);
}

TEST_CASE("gate: verdict_name covers every enumerator", "[gate]")
{
    REQUIRE(std::string(mth::verdict_name(mth::GateVerdict::Pending)) == "Pending");
    REQUIRE(std::string(mth::verdict_name(mth::GateVerdict::Clear)) == "Clear");
    REQUIRE(std::string(mth::verdict_name(mth::GateVerdict::Refused)) == "Refused");
}
