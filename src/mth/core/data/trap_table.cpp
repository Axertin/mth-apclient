#include "mth/core/data/trap_table.hpp"

#include "MinaModEnums.h"

namespace mth
{

namespace
{

constexpr TrapDef make_trap(int modifier_index, const char *label, float seconds)
{
    return TrapDef{modifier_index, label, seconds};
}

// Builds one row from the modifier's own kCheat_* name, so the label has exactly one source.
// MTH_TRAP(Mirror, 30.0f) is make_trap(kCheat_Mirror, "Mirror", 30.0f):
// the ## paste forms the token kCheat_Mirror, which is then itself macro-expanded to 204,
// and the # stringizes the bare argument. The duration stays a per-row argument so one trap
// can be retuned without touching the others.
#define MTH_TRAP(name, seconds) make_trap(kCheat_##name, #name, seconds)

constexpr TrapDef kTrapTable[] = {
    MTH_TRAP(FlipControls, kDefaultTrapSeconds),      // input axes reversed
    MTH_TRAP(FloorIsLava, kDefaultTrapSeconds),       // floor is lava
    MTH_TRAP(Giant, kDefaultTrapSeconds),             // player render scale
    MTH_TRAP(Giant2, kDefaultTrapSeconds),            // player render scale
    MTH_TRAP(GiantEnemies, kDefaultTrapSeconds),      // enemy render scale
    MTH_TRAP(GiantEnemies2, kDefaultTrapSeconds),     // enemy render scale
    MTH_TRAP(StartInvisible, kDefaultTrapSeconds),    // Mina invisible
    MTH_TRAP(NoHUD, kDefaultTrapSeconds),             // HUD hidden
    MTH_TRAP(RotateCamera, kDefaultTrapSeconds / 5),  // continuous camera roll
    MTH_TRAP(RotateCameraInput, kDefaultTrapSeconds), // camera tilts with horizontal input
    MTH_TRAP(Mirror, kDefaultTrapSeconds),            // scene flipped horizontally, controls flip with it
    MTH_TRAP(Upsidedown, kDefaultTrapSeconds),        // scene flipped vertically
};

#undef MTH_TRAP

} // namespace

std::span<const TrapDef> traps()
{
    return std::span<const TrapDef>(kTrapTable);
}

const TrapDef *trap_for_modifier(int modifier_index)
{
    for (const auto &t : kTrapTable)
        if (t.modifier_index == modifier_index)
            return &t;
    return nullptr;
}

} // namespace mth
