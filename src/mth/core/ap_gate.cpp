#include "mth/core/ap_gate.hpp"

namespace mth
{

namespace
{

bool static_checks_passed(const GateInputs &in)
{
    return in.mod_api_present && in.symbols_resolved && in.item_table_shape_ok && in.layout_probes_ok;
}

bool liveness_expired(const GateInputs &in)
{
    return !in.worldupdate_observed && in.ticks_since_probe_installed >= kLivenessTimeoutTicks;
}

} // namespace

GateVerdict evaluate(const GateInputs &in)
{
    if (!static_checks_passed(in))
        return GateVerdict::Refused;
    if (in.worldupdate_observed)
        return GateVerdict::Clear;
    if (liveness_expired(in))
        return GateVerdict::Refused;
    return GateVerdict::Pending;
}

const char *verdict_name(GateVerdict v)
{
    switch (v)
    {
    case GateVerdict::Pending:
        return "Pending";
    case GateVerdict::Clear:
        return "Clear";
    case GateVerdict::Refused:
        return "Refused";
    }
    return "Unknown";
}

std::string refusal_reason(const GateInputs &in)
{
    // Ordered most-fundamental first: the first failure is the one worth reporting, since a
    // missing mod API also explains every downstream check that depends on it.
    if (!in.mod_api_present)
        return "the game's mod API is unavailable (is this the experimental-modding build?)";
    if (!in.symbols_resolved)
        return "one or more required game functions could not be located (game updated?)";
    if (!in.item_table_shape_ok)
        return "the game's item table does not match the expected layout (game updated?)";
    if (!in.layout_probes_ok)
        return "a game struct layout probe failed (game updated?)";
    if (liveness_expired(in))
        return "the game never ran the mod's WorldUpdate hook";
    return std::string();
}

GateVerdict GateLatch::update(const GateInputs &in)
{
    if (settled())
        return verdict_;
    verdict_ = evaluate(in);
    return verdict_;
}

} // namespace mth
