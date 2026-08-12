#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "mth/core/save/zip_archive.hpp"

namespace mth
{

// "ap_<seed>_<slot>.zip", seed/slot sanitized. Unlike ap_save_filename there is no hash suffix: the
// container's manifest carries the raw key parts, so a sanitization collision is caught on load by
// comparing identity rather than by making the name unique.
std::string ap_bundle_filename(std::string_view seed, std::string_view slot);

// One container per (seed, slot) holding the game save blob and the AP state together, so the two
// can never disagree about where a run got to. Reads fall back to the pre-container layout and the
// first write migrates forward; the old files are left in place.
class ApSaveBundleStore
{
  public:
    struct LegacyDirs
    {
        std::filesystem::path state_dir;  // where the loose .state used to live
        std::filesystem::path ycsave_dir; // where the loose .ycsave used to live
    };

    ApSaveBundleStore(std::filesystem::path dir, LegacyDirs legacy);

    [[nodiscard]] std::filesystem::path path_for(std::string_view seed, std::string_view slot) const;

    // The game save blob. Named to match the superseded ApSaveStore so the takeover call sites did
    // not have to change.
    [[nodiscard]] std::optional<std::string> load(std::string_view seed, std::string_view slot) const;
    bool store(std::string_view seed, std::string_view slot, std::string_view blob);

    [[nodiscard]] std::optional<std::string> load_state(std::string_view seed, std::string_view slot) const;
    bool store_state(std::string_view seed, std::string_view slot, std::string_view text);

  private:
    // One session is live at a time, so a key change reloads rather than growing a map.
    struct Cache
    {
        bool loaded{false};
        std::string seed;
        std::string slot;
        std::optional<std::string> game_save;
        std::optional<std::string> ap_state;
        std::map<std::string, zip::Blob> blobs; // compressed form, reused for unchanged entries
    };

    void ensure_loaded(std::string_view seed, std::string_view slot) const;
    bool persist() const;

    std::filesystem::path dir_;
    LegacyDirs legacy_;
    mutable Cache cache_;
    mutable bool dir_ready_{false};
};

} // namespace mth
