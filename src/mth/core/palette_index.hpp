#pragma once

#include <cstdint>
#include <optional>

namespace mth
{

// Which palette entry to overwrite so a wanted color survives the engine's palette mapping.
// api_index is what the mod API's PaletteGetIndex answered for that color against the widget's
// lookup palette; the engine resolves a non-member color to entry 0, so a negative answer means
// the same thing. Nullopt when the entry does not exist: PaletteWriteIndex is unbounded and would
// write past the color array.
[[nodiscard]] constexpr std::optional<std::uint32_t> palette_target_index(std::int32_t api_index, std::uint32_t palette_width)
{
    const std::uint32_t idx = api_index < 0 ? 0u : static_cast<std::uint32_t>(api_index);
    if (idx >= palette_width)
        return std::nullopt;
    return idx;
}

} // namespace mth
