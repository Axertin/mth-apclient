#include "mth/core/offsets.hpp"

namespace mth
{

namespace
{

// Unknown / unmapped builds: zeros -> GameHooks installs nothing.
constexpr GameOffsets kNoOffsets{0, 0, 0, 0};

constexpr GameOffsets kLinuxV1{
    0x00c96800, // Game::FixedUpdate()
    0x00c979e0, // Game::Update(float)
    0x00ec6420, // World::Update(ycUpdateQueueContext*)
    0x0031f6f0, // ycUpdateQueue::Update(float)
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

} // namespace mth
