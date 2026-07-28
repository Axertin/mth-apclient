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

ApSaveStore::ApSaveStore(std::filesystem::path dir) : dir_(std::move(dir))
{
}

std::filesystem::path ApSaveStore::path_for(std::string_view seed, std::string_view slot) const
{
    return dir_ / ap_save_filename(seed, slot);
}

std::optional<std::string> ApSaveStore::load(std::string_view seed, std::string_view slot) const
{
    std::ifstream in(path_for(seed, slot), std::ios::binary);
    if (!in)
        return std::nullopt;
    std::string blob(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>{});
    if (!looks_like_save_blob(blob))
        return std::nullopt;
    return blob;
}

bool ApSaveStore::store(std::string_view seed, std::string_view slot, std::string_view blob)
{
    if (!looks_like_save_blob(blob))
        return false;

    std::error_code ec;
    if (!dir_ready_)
    {
        std::filesystem::create_directories(dir_, ec); // once: store() runs on the game's save cadence
        if (ec)
            return false;
        dir_ready_ = true;
    }

    const std::filesystem::path final_path = path_for(seed, slot);
    std::filesystem::path tmp_path = final_path;
    tmp_path += ".tmp";

    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
        if (!out)
            return false;
    }

    std::filesystem::rename(tmp_path, final_path, ec);
    if (ec)
    {
        std::filesystem::remove(tmp_path, ec);
        return false;
    }
    return true;
}

} // namespace mth
