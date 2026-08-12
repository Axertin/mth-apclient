#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mth::zip
{

inline constexpr std::uint16_t kStore = 0;
inline constexpr std::uint16_t kDeflate = 8;

// One entry, already compressed and ready to emit. Held rather than the plain bytes so an unchanged
// entry can be re-emitted without paying deflate again on the game thread.
struct Blob
{
    std::string name;
    std::string stored; // deflate output, or the raw bytes when method == kStore
    std::uint32_t crc32{};
    std::uint32_t uncompressed_size{};
    std::uint16_t method{kStore};
};

struct Entry
{
    std::string name;
    std::string data; // uncompressed
};

// Falls back to kStore when deflate does not shrink the input, so a container is never larger than
// its payload.
[[nodiscard]] Blob compress(std::string_view name, std::string_view data);

// Emits entries in the order given. Timestamps are fixed at the DOS epoch, so identical input
// produces identical bytes.
[[nodiscard]] std::string write(const std::vector<Blob> &blobs);

// nullopt on malformed, truncated, CRC-mismatched, encrypted, zip64, or unsafely-named input, so a
// damaged container reads as "no save" rather than as garbage.
[[nodiscard]] std::optional<std::vector<Entry>> read(std::string_view image);

} // namespace mth::zip
