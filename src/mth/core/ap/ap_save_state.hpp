#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace mth
{

// Per-(seed, slot) run state: checked location indices and granted item indices. Staged through
// the store seam, which holds it until the game saves so the two can never disagree.
// Sets rather than cursors: robust to gaps/out-of-order. Received-items list is
// server state and is never persisted.
class ApSaveState
{
  public:
    // IO seam: the save container owns the file, so this class owns only the format. Loads through
    // `load` immediately; missing/corrupt content => empty state.
    using LoadFn = std::function<std::optional<std::string>()>;
    using StoreFn = std::function<void(std::string_view)>;

    ApSaveState(LoadFn load, StoreFn store);

    // Direct-to-file, for callers with no container.
    explicit ApSaveState(std::filesystem::path path);

    [[nodiscard]] std::string serialize() const;
    void deserialize(std::string_view text);

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

    void stage() const; // held in memory until the game saving commits the bundle

  private:
    LoadFn load_fn_;
    StoreFn store_fn_;
    std::set<int> checked_;
    std::set<int> granted_;
    int game_slot_{-1};
};

} // namespace mth
