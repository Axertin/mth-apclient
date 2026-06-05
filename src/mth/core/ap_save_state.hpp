#pragma once

#include <filesystem>
#include <set>

namespace mth
{

// Per-(seed, slot) durable AP state. Two authoritative sets: location indices we have
// CHECKED (outbound + respawn authority, used by a later plan) and AP received-item
// indices we have GRANTED (inbound dedup). Sets, not cursors: robust to gaps/out-of-order.
// The received-items LIST is server state (re-synced on connect), so it is never persisted.
// Pure logic: the only OS dependency is std::filesystem/fstream (allowed in mthap_core).
class ApSaveState
{
  public:
    // Constructs and immediately loads from `path` (missing/corrupt file => empty).
    explicit ApSaveState(std::filesystem::path path);

    [[nodiscard]] bool is_checked(int location_index) const;
    [[nodiscard]] bool is_granted(int item_index) const;
    void mark_checked(int location_index);
    void mark_granted(int item_index);

    // Atomically (write-temp-then-rename) persists both sets to the path.
    void save() const;

  private:
    void load();

    std::filesystem::path path_;
    std::set<int> checked_;
    std::set<int> granted_;
};

} // namespace mth
