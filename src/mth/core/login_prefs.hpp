#pragma once

#include <filesystem>
#include <string>

namespace mth
{

// Last-used AP connection target, persisted so the login window can auto-fill it.
// Password is deliberately never stored.
class LoginPrefs
{
  public:
    // Loads from `path` immediately; missing/corrupt file => empty prefs.
    explicit LoginPrefs(std::filesystem::path path);

    [[nodiscard]] const std::string &server() const
    {
        return server_;
    }
    [[nodiscard]] const std::string &slot() const
    {
        return slot_;
    }

    void set(std::string server, std::string slot);

    void save() const; // write-tmp-then-rename

  private:
    void load();

    std::filesystem::path path_;
    std::string server_;
    std::string slot_;
};

} // namespace mth
