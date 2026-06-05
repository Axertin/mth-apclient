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

    void save() const; // write-tmp-then-rename

  private:
    void load();

    std::filesystem::path path_;
    std::set<int> checked_;
    std::set<int> granted_;
};

} // namespace mth
