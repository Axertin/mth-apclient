#pragma once

#include <map>
#include <string>
#include <utility>

namespace mth
{

// One scouted shop location: what AP item it holds, for whom, and its classification flags.
// Strings are resolved in the link layer (net thread) and carried here as plain data.
struct ScoutInfo
{
    int collection_slot{};
    std::string item_name;
    std::string player_alias;
    std::string player_game;
    unsigned item_flags{};
    bool is_self{};
};

// Description-widget text for a scouted location: recipient + their game, or a self form.
[[nodiscard]] inline std::string format_scout_desc(const ScoutInfo &info)
{
    if (info.is_self)
        return "for you";
    return "for " + info.player_alias + " (" + info.player_game + ")";
}

// Game-thread-only: filled from ApScoutInfo events, read by the shop text hook. No locking.
class ScoutRegistry
{
  public:
    void record(ScoutInfo info)
    {
        const int slot = info.collection_slot;
        by_slot_[slot] = std::move(info);
    }

    [[nodiscard]] const ScoutInfo *lookup(int collection_slot) const
    {
        const auto it = by_slot_.find(collection_slot);
        return it != by_slot_.end() ? &it->second : nullptr;
    }

    void clear()
    {
        by_slot_.clear();
    }

  private:
    std::map<int, ScoutInfo> by_slot_;
};

} // namespace mth
