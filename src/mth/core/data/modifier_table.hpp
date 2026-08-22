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

// The AP lockdown's deny list: modifiers a randomized run cannot survive. Everything outside it
// stays the player's to pick, difficulty and cosmetic sets included.
[[nodiscard]] bool is_ap_denied(int idx);

} // namespace mth
