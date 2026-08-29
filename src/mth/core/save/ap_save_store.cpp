#include "mth/core/save/ap_save_store.hpp"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <system_error>

namespace mth
{
namespace
{
constexpr std::string_view kHeader = "[YCD Version: 1]";
constexpr std::string_view kBodyTag = "SaveSlot";

// FNV-1a 64-bit: fixed algorithm (not std::hash) so the filename is stable across platforms,
// library versions, and rebuilds.
std::uint64_t fnv1a64(std::string_view data, std::uint64_t hash)
{
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    for (const char c : data)
    {
        hash ^= static_cast<unsigned char>(c);
        hash *= kPrime;
    }
    return hash;
}

std::string hex16(std::uint64_t value)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i)
    {
        out[static_cast<std::size_t>(i)] = kDigits[value & 0xF];
        value >>= 4;
    }
    return out;
}

// Hashes the raw (unsanitized) parts so sanitization collisions (e.g. "a b" and "a_b") still
// differ. NUL separator so ("ab","c") and ("a","bc") hash differently.
std::uint64_t key_hash(std::string_view seed, std::string_view slot)
{
    constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
    std::uint64_t hash = fnv1a64(seed, kOffsetBasis);
    hash = fnv1a64(std::string_view("\0", 1), hash);
    hash = fnv1a64(slot, hash);
    return hash;
}
} // namespace

std::string sanitize_save_key_part(std::string_view raw)
{
    std::string out;
    out.reserve(raw.size());
    for (const char c : raw)
    {
        const auto uc = static_cast<unsigned char>(c);
        const bool safe = (std::isalnum(uc) != 0) || c == '-' || c == '_';
        out.push_back(safe ? c : '_');
    }
    if (out.empty())
        return "unnamed";
    return out;
}

std::string ap_save_filename(std::string_view seed, std::string_view slot)
{
    return "ap_" + sanitize_save_key_part(seed) + "_" + sanitize_save_key_part(slot) + "_" + hex16(key_hash(seed, slot)) + ".ycsave";
}

bool looks_like_save_blob(std::string_view blob)
{
    return blob.size() > kHeader.size() && blob.compare(0, kHeader.size(), kHeader) == 0 && blob.find(kBodyTag) != std::string_view::npos;
}

} // namespace mth
