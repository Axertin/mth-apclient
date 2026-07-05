#include "mth/core/data/modifier_table.hpp"

#include "mth/core/modifier_config.hpp" // kCheatCount

namespace mth
{

CheatClass class_of(int idx)
{
    if (idx < 0 || idx >= kCheatCount)
        return CheatClass::Invalid;
    // Checked in ascending index order so the non-overlap of the ranges is obvious.
    if (idx >= 122 && idx <= 125)
        return CheatClass::Randomizer;
    if (idx == 19 || idx == 54 || (idx >= 128 && idx <= 172))
        return CheatClass::Grant;
    if (idx >= 214 && idx <= 253)
        return CheatClass::Combo;
    // Remaining valid indices (0-18, 20-53, 55-121, 126-127, 173-213) write no apply-time field.
    return CheatClass::Continuous;
}

bool is_safe(int idx)
{
    return class_of(idx) == CheatClass::Continuous;
}

bool is_cosmetic(int idx)
{
    // Conservative visual/audio/UI-only set (from the master index table). Everything else is
    // treated as gameplay so lockdown can't be bypassed by a mislabel. Expandable after play-test.
    switch (idx)
    {
    case 185: // GlobalPal
    case 186: // CloakColor1
    case 187: // CloakColor2
    case 188: // CloakColor3
    case 189: // IdleDance
    case 198: // RandomMusic
    case 206: // NYC (visual theme)
    case 207: // TavernName
    case 208: // TavernColor
    case 209: // TavernTheme
    case 210: // TavernMusic
    case 211: // TavernGhost
    case 212: // PortraitAccessory
    case 213: // CustomFlower
        return true;
    default:
        return false;
    }
}

bool is_gameplay(int idx)
{
    return idx >= 0 && idx < kCheatCount && !is_cosmetic(idx);
}

} // namespace mth
