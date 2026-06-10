#pragma once

namespace mth
{

// Classification of a modifier index, from decompiling ApplyNewFileCheat (switch(idx-0x13)).
enum class CheatClass
{
    Invalid,    // idx outside 0..253
    Continuous, // no apply-time field write; safe to set/toggle live
    Grant,      // one-shot irreversible save mutation (idx 19, 54, 128..172)
    Randomizer, // sets randomize flags; shuffle runs in ActivateSaveSlot (idx 122..125)
    Combo,      // recurses into sub-cheats via ApplyFileCheats (idx 214..253)
};

[[nodiscard]] CheatClass class_of(int idx);

// Continuous == safe to force on a save and toggle live. Returns false for invalid indices.
[[nodiscard]] bool is_safe(int idx);

// Purely visual/audio/UI modifiers the player keeps even when locked down. Conservative
// allow-list (when unsure, treat as gameplay so lockdown stays airtight).
[[nodiscard]] bool is_cosmetic(int idx);

// Lockdown target: any valid, non-cosmetic index (includes grants/combos/randomizer).
[[nodiscard]] bool is_gameplay(int idx);

} // namespace mth
