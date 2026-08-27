#include <cstdlib>
#include <filesystem>

#include "pal/pal_log.hpp"

namespace fs = std::filesystem;

namespace pal
{

fs::path log_dir()
{
    if (const char *appdata = std::getenv("LOCALAPPDATA"); appdata && *appdata)
        return fs::path(appdata) / "mth-apclient";
    if (const char *tmp = std::getenv("TEMP"); tmp && *tmp)
        return fs::path(tmp) / "mth-apclient";
    return fs::temp_directory_path() / "mth-apclient";
}

} // namespace pal
