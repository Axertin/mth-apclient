#include "mth/core/ap_save_state.hpp"

#include <fstream>
#include <string>

namespace mth
{

// File format: "c <int>" = checked location, "g <int>" = granted item, "s <int>" = game save slot
ApSaveState::ApSaveState(std::filesystem::path path) : path_(std::move(path))
{
    load();
}

void ApSaveState::load()
{
    std::ifstream in(path_);
    if (!in)
        return;
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
    std::error_code ec;
    std::filesystem::create_directories(path_.parent_path(), ec);

    auto tmp = path_;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out)
            return;
        for (int v : checked_)
            out << "c " << v << '\n';
        for (int v : granted_)
            out << "g " << v << '\n';
        if (game_slot_ >= 0)
            out << "s " << game_slot_ << '\n';
    }
    std::filesystem::rename(tmp, path_, ec); // atomic replace
}

} // namespace mth
