#pragma once

#include <string>

namespace mth
{

// All-or-nothing: a client that half-works produces unwinnable seeds and, because AP is
// multiplayer, releases items into a multiworld the local run can never reciprocate.
enum class GateVerdict
{
    Pending, // static checks passed; waiting for proof the native mod hooks fire
    Clear,
    Refused
};

// Only worldupdate_observed is temporal. The rest are properties of the loaded binary: the mod
// hooks no heap addresses, so nothing it depends on can drift while the game runs.
struct GateInputs
{
    bool mod_api_present{false};
    bool symbols_resolved{false};
    bool item_table_shape_ok{false};
    bool layout_probes_ok{false};
    bool revision_known{false}; // informational: drives a banner, never the verdict
    bool worldupdate_observed{false};
    int ticks_since_probe_installed{0};
};

// WorldUpdate ticks at the title screen, reached within a second or two of launch.
inline constexpr int kLivenessTimeoutTicks = 600;

[[nodiscard]] GateVerdict evaluate(const GateInputs &in);

[[nodiscard]] const char *verdict_name(GateVerdict v);

// Empty unless refused. Names the first failing input, so the banner alone is diagnosable.
[[nodiscard]] std::string refusal_reason(const GateInputs &in);

// Terminal: once the verdict leaves Pending it never changes, so a late input cannot re-arm AP
// behavior that was already refused.
class GateLatch
{
  public:
    GateVerdict update(const GateInputs &in);

    [[nodiscard]] GateVerdict verdict() const
    {
        return verdict_;
    }

    [[nodiscard]] bool settled() const
    {
        return verdict_ != GateVerdict::Pending;
    }

  private:
    GateVerdict verdict_{GateVerdict::Pending};
};

} // namespace mth
