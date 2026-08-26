#include "mth/core/data/trap_table.hpp"

namespace mth
{

namespace
{

constexpr TrapDef kTrapTable[] = {
    {204, "Mirror Mode", kDefaultTrapSeconds},     // scene flipped horizontally, controls flip with it
    {205, "Upsidedown Mode", kDefaultTrapSeconds}, // scene flipped vertically
    {202, "Spin!", kDefaultTrapSeconds},           // continuous camera roll
    {203, "Lean!", kDefaultTrapSeconds},           // camera tilts with horizontal input
    {197, "No HUD", kDefaultTrapSeconds},          // HUD hidden
    {195, "Invisibility", kDefaultTrapSeconds},    // Mina invisible
    {190, "1.5x Giant!", kDefaultTrapSeconds},     // player render scale
    {191, "2x Giant!", kDefaultTrapSeconds},       // player render scale
};

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
