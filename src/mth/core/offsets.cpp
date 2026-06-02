#include "mth/core/offsets.hpp"

namespace mth
{

namespace
{

// Unknown / unmapped builds: zeros -> GameHooks installs nothing.
constexpr GameOffsets kNoOffsets{0, 0, 0, 0};

// Linux build 1.0.5, GNU BuildID 958b6568... (the live Steam build, 2026-06-02
// rebuild). ELF symbol vaddrs == load-base-relative offsets. Functions shift on
// each rebuild; re-derive per build via `nm -C MinaTheHollower`.
constexpr GameOffsets kLinuxV1{
    0x00c97000, // Game::FixedUpdate()
    0x00c981e0, // Game::Update(float)
    0x00ec6cb0, // World::Update(ycUpdateQueueContext*)
    0x0031f620, // ycUpdateQueue::Update(float)
};

constexpr RandoOffsets kNoRando{0};

constexpr RandoOffsets kLinuxV1Rando{
    0x00b3c590, // Items::OnPickupDone
};

constexpr OverlayOffsets kNoOverlay{0};

// ProcessSDLEvent(SDL_Event&), build 958b6568 (1.0.5, 2026-06-02). main's
// SDL_WaitEvent loop calls this for every event, so it is where input (incl. our
// F1 toggle) flows. ELF symbol vaddr from
// `nm -C MinaTheHollower | grep 'ProcessSDLEvent'`.
constexpr OverlayOffsets kLinuxV1Overlay{
    0x0020cca0, // ProcessSDLEvent(SDL_Event&)  (verified via `nm -C`)
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

const OverlayOffsets &overlay_offsets_for(Build build)
{
    switch (build)
    {
    case Build::Linux_v1_0:
        return kLinuxV1Overlay;
    case Build::Windows_v1_0:
    case Build::Unknown:
        break;
    }
    return kNoOverlay;
}

} // namespace mth
