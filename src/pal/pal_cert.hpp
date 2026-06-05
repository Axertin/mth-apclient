#pragma once

#include <filesystem>
#include <optional>

namespace pal
{

// Returns nullopt if no bundle is found/writable.
std::optional<std::filesystem::path> ca_bundle_path();

} // namespace pal
