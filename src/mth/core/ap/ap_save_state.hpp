#pragma once

#include <filesystem>
#include <set>

namespace mth
{

// Per-(seed, slot) durable state: checked location indices and granted item indices.
// Sets rather than cursors: robust to gaps/out-of-order. Received-items list is
// server state and is never persisted.
class ApSaveState
{
  public:
    // Loads from `path` immediately; missing/corrupt file => empty state.
    explicit ApSaveState(std::filesystem::path path);

    [[nodiscard]] bool is_checked(int location_index) const;
    [[nodiscard]] bool is_granted(int item_index) const;
    [[nodiscard]] const std::set<int> &checked() const
    {
        return checked_;
    }
    void mark_checked(int location_index);
    void mark_granted(int item_index);

    // The game's 0-based save-slot index this AP game lives on (-1 = not yet known). Lets the mod
    // enforce modifiers on only this slot across sessions, never a vanilla profile.
    [[nodiscard]] int game_slot() const
    {
        return game_slot_;
    }
    void set_game_slot(int slot)
    {
        game_slot_ = slot;
    }

    // True once this AP game has recorded anything. An unbound state file with progress was played
    // before the save-slot binding existed; an unbound one without progress is simply a fresh seed
    // that has not started its game yet. See ap_bind_legacy_unbound().
    [[nodiscard]] bool has_progress() const
    {
        return !checked_.empty() || !granted_.empty();
    }

    void save() const; // write-tmp-then-rename

  private:
    void load();

    std::filesystem::path path_;
    std::set<int> checked_;
    std::set<int> granted_;
    int game_slot_{-1};
};

} // namespace mth
