#pragma once

#include <filesystem>
#include <optional>

namespace pal
{

// Path to a CA-certificate bundle file for TLS verification, or nullopt if none
// is available. Linux: probes well-known system locations. Windows: materializes
// the system ROOT store to a PEM and returns its path.
std::optional<std::filesystem::path> ca_bundle_path();

} // namespace pal
