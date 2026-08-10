#pragma once

#include <cstdint>
#include <optional>

namespace mth
{

// Which palette entry to overwrite so a color survives the engine's palette mapping. A negative
// api_index is PaletteGetIndex's miss answer, which the engine resolves to entry 0. Nullopt when
// the entry does not exist: PaletteWriteIndex is unbounded.
[[nodiscard]] constexpr std::optional<std::uint32_t> palette_target_index(std::int32_t api_index, std::uint32_t palette_width)
{
    const std::uint32_t idx = api_index < 0 ? 0u : static_cast<std::uint32_t>(api_index);
    if (idx >= palette_width)
        return std::nullopt;
    return idx;
}

} // namespace mth
