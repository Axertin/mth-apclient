#include <cstdlib>
#include <filesystem>

#include "pal/pal_log.hpp"

namespace fs = std::filesystem;

namespace pal
{

fs::path log_dir()
{
    const char *xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && *xdg)
        return fs::path(xdg) / "mth-apclient";
    const char *home = std::getenv("HOME");
    if (home && *home)
        return fs::path(home) / ".local/share/mth-apclient";
    return fs::temp_directory_path() / "mth-apclient";
}

} // namespace pal
