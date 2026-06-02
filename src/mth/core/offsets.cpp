#include "mth/core/offsets.hpp"

namespace mth
{

namespace
{

// Unknown / unmapped builds: zeros -> GameHooks installs nothing.
constexpr GameOffsets kNoOffsets{0, 0, 0, 0};

// Linux build 1.0.5 [r148053] (GNU BuildID 45919e54...; the live Steam build).
// ELF symbol vaddrs == load-base-relative offsets. (r147980 differed by a small
// shift, e.g. FixedUpdate 0x00c96800; re-derive per build via readelf.)
constexpr GameOffsets kLinuxV1{
    0x00c97960, // Game::FixedUpdate()
    0x00c98b40, // Game::Update(float)
    0x00ec6720, // World::Update(ycUpdateQueueContext*)
    0x0031f620, // ycUpdateQueue::Update(float)
};

constexpr RandoOffsets kNoRando{0};

constexpr RandoOffsets kLinuxV1Rando{
    0x00b3bf70, // Items::OnPickupDone
};

} // namespace

const GameOffsets &offsets_for(Build build)
{
    switch (build)
    {
    case Build::Linux_v1_0:
        return kLinuxV1;
    case Build::Windows_v1_0:
        // Windows offsets not mapped yet - the PE ships no symbols, so these
        // get ported from the named Linux binary later. Until then, no hooks.
    case Build::Unknown:
        break;
    }
    return kNoOffsets;
}

const RandoOffsets &rando_offsets_for(Build build)
{
    switch (build)
    {
    case Build::Linux_v1_0:
        return kLinuxV1Rando;
    case Build::Windows_v1_0:
    case Build::Unknown:
        break;
    }
    return kNoRando;
}

} // namespace mth
