#include "mth/core/login_prefs.hpp"

#include <fstream>
#include <utility>

namespace mth
{

// File format: "server <host:port>" / "slot <name>", one per line. The value is the rest of
// the line, not a token: AP slot names routinely contain spaces.
LoginPrefs::LoginPrefs(std::filesystem::path path) : path_(std::move(path))
{
    load();
}

void LoginPrefs::load()
{
    std::ifstream in(path_);
    if (!in)
        return;
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto space = line.find(' ');
        if (space == std::string::npos)
            continue;
        const std::string key = line.substr(0, space);
        std::string value = line.substr(space + 1);
        if (value.empty())
            continue;
        if (key == "server")
            server_ = std::move(value);
        else if (key == "slot")
            slot_ = std::move(value);
    }
}

void LoginPrefs::set(std::string server, std::string slot)
{
    server_ = std::move(server);
    slot_ = std::move(slot);
}

void LoginPrefs::save() const
{
    std::error_code ec;
    std::filesystem::create_directories(path_.parent_path(), ec);

    auto tmp = path_;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out)
            return;
        if (!server_.empty())
            out << "server " << server_ << '\n';
        if (!slot_.empty())
            out << "slot " << slot_ << '\n';
    }
    std::filesystem::rename(tmp, path_, ec); // atomic replace
}

} // namespace mth
