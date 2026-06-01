#include "mth/core/build_id.hpp"

namespace mth
{

Build detect_build(std::string_view build_id)
{
    if (build_id == kLinuxV1BuildId)
        return Build::Linux_v1_0;
    return Build::Unknown;
}

std::string_view build_name(Build build)
{
    switch (build)
    {
    case Build::Linux_v1_0:
        return "Linux v1.0";
    case Build::Unknown:
        break;
    }
    return "Unknown";
}

} // namespace mth
