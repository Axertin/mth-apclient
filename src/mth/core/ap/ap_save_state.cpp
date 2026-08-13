#include "mth/core/ap/ap_save_state.hpp"

#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

namespace mth
{

// File format: "c <int>" = checked location, "g <int>" = granted item, "s <int>" = game save slot
ApSaveState::ApSaveState(LoadFn load, StoreFn store) : load_fn_(std::move(load)), store_fn_(std::move(store))
{
    if (load_fn_)
    {
        if (const auto text = load_fn_())
            deserialize(*text);
    }
}

ApSaveState::ApSaveState(std::filesystem::path path)
    : ApSaveState(
          [path]() -> std::optional<std::string>
          {
              std::ifstream in(path, std::ios::binary);
              if (!in)
                  return std::nullopt;
              return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>{});
          },
          [path](std::string_view text)
          {
              std::error_code ec;
              std::filesystem::create_directories(path.parent_path(), ec);
              auto tmp = path;
              tmp += ".tmp";
              {
                  std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
                  if (!out)
                      return;
                  out.write(text.data(), static_cast<std::streamsize>(text.size()));
                  // Explicit: the buffer is only flushed on close, so the destructor would swallow a
                  // failed write and the rename would publish a truncated file.
                  out.close();
                  if (!out)
                  {
                      std::filesystem::remove(tmp, ec);
                      return;
                  }
              }
              std::filesystem::rename(tmp, path, ec); // atomic replace
          })
{
}

std::string ApSaveState::serialize() const
{
    std::string out;
    for (int v : checked_)
        out += "c " + std::to_string(v) + "\n";
    for (int v : granted_)
        out += "g " + std::to_string(v) + "\n";
    if (game_slot_ >= 0)
        out += "s " + std::to_string(game_slot_) + "\n";
    return out;
}

void ApSaveState::deserialize(std::string_view text)
{
    // Replaces rather than merges, so it is the inverse of serialize() however many times it runs.
    checked_.clear();
    granted_.clear();
    game_slot_ = -1;

    std::istringstream in{std::string(text)};
    char tag = 0;
    int value = 0;
    while (in >> tag >> value)
    {
        if (tag == 'c')
            checked_.insert(value);
        else if (tag == 'g')
            granted_.insert(value);
        else if (tag == 's')
            game_slot_ = value;
    }
}

bool ApSaveState::is_checked(int location_index) const
{
    return checked_.count(location_index) != 0;
}

bool ApSaveState::is_granted(int item_index) const
{
    return granted_.count(item_index) != 0;
}

void ApSaveState::mark_checked(int location_index)
{
    checked_.insert(location_index);
}

void ApSaveState::mark_granted(int item_index)
{
    granted_.insert(item_index);
}

void ApSaveState::save() const
{
    if (store_fn_)
        store_fn_(serialize());
}

} // namespace mth
