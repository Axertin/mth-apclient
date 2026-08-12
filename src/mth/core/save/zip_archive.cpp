#include "mth/core/save/zip_archive.hpp"

#include <utility>

#include <zlib.h>

namespace mth::zip
{
namespace
{
constexpr std::uint32_t kLocalSig = 0x04034b50u;
constexpr std::uint32_t kCentralSig = 0x02014b50u;
constexpr std::uint32_t kEocdSig = 0x06054b50u;
constexpr std::size_t kLocalHeaderSize = 30;
constexpr std::size_t kCentralHeaderSize = 46;
constexpr std::size_t kEocdSize = 22;
constexpr std::uint16_t kVersionNeeded = 20;    // 2.0: the deflate floor
constexpr std::uint16_t kDosEpochDate = 0x0021; // 1980-01-01
constexpr std::uint16_t kDosEpochTime = 0x0000;

void put16(std::string &out, std::uint16_t v)
{
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void put32(std::string &out, std::uint32_t v)
{
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
    out.push_back(static_cast<char>((v >> 16) & 0xFF));
    out.push_back(static_cast<char>((v >> 24) & 0xFF));
}

std::uint16_t get16(std::string_view s, std::size_t off)
{
    return static_cast<std::uint16_t>(static_cast<unsigned char>(s[off]) | (static_cast<unsigned char>(s[off + 1]) << 8));
}

std::uint32_t get32(std::string_view s, std::size_t off)
{
    return static_cast<std::uint32_t>(static_cast<unsigned char>(s[off])) | (static_cast<std::uint32_t>(static_cast<unsigned char>(s[off + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[off + 2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[off + 3])) << 24);
}

// Rejects anything that could direct an extractor outside its target directory. Our own names are
// flat, but a container on disk is user-writable.
bool name_is_safe(std::string_view name)
{
    if (name.empty() || name.size() > 4096)
        return false;
    if (name.front() == '/' || name.front() == '\\')
        return false;
    if (name.find('\\') != std::string_view::npos)
        return false;
    if (name.find(':') != std::string_view::npos)
        return false;
    if (name.find('\0') != std::string_view::npos)
        return false;

    std::size_t start = 0;
    for (;;)
    {
        const std::size_t end = name.find('/', start);
        const std::string_view part = name.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (part == "..")
            return false;
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return true;
}

std::string deflate_raw(std::string_view data)
{
    z_stream zs{};
    // windowBits -15: raw deflate, no zlib wrapper, which is what a zip member holds.
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return {};

    std::string out;
    out.resize(deflateBound(&zs, static_cast<uLong>(data.size())));
    zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
    zs.avail_in = static_cast<uInt>(data.size());
    zs.next_out = reinterpret_cast<Bytef *>(out.data());
    zs.avail_out = static_cast<uInt>(out.size());

    const int rc = deflate(&zs, Z_FINISH);
    const std::size_t produced = out.size() - zs.avail_out;
    deflateEnd(&zs);
    if (rc != Z_STREAM_END)
        return {};
    out.resize(produced);
    return out;
}

// Sized rather than streaming: the uncompressed length is recorded in the header, so a stream that
// does not fill it exactly is corrupt.
bool inflate_raw(std::string_view data, std::uint32_t expected, std::string &out)
{
    out.clear();
    out.resize(expected);
    if (expected == 0)
        return true;

    z_stream zs{};
    if (inflateInit2(&zs, -15) != Z_OK)
        return false;
    zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
    zs.avail_in = static_cast<uInt>(data.size());
    zs.next_out = reinterpret_cast<Bytef *>(out.data());
    zs.avail_out = static_cast<uInt>(out.size());

    const int rc = inflate(&zs, Z_FINISH);
    const bool exact = (zs.avail_out == 0 && zs.avail_in == 0);
    inflateEnd(&zs);
    return rc == Z_STREAM_END && exact;
}

std::uint32_t crc_of(std::string_view data)
{
    return static_cast<std::uint32_t>(crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef *>(data.data()), static_cast<uInt>(data.size())));
}
} // namespace

Blob compress(std::string_view name, std::string_view data)
{
    Blob blob;
    blob.name = std::string(name);
    blob.uncompressed_size = static_cast<std::uint32_t>(data.size());
    blob.crc32 = crc_of(data);

    std::string deflated = deflate_raw(data);
    if (!deflated.empty() && deflated.size() < data.size())
    {
        blob.method = kDeflate;
        blob.stored = std::move(deflated);
    }
    else
    {
        blob.method = kStore;
        blob.stored = std::string(data);
    }
    return blob;
}

std::string write(const std::vector<Blob> &blobs)
{
    std::string out;
    std::vector<std::uint32_t> offsets;
    offsets.reserve(blobs.size());

    for (const Blob &b : blobs)
    {
        offsets.push_back(static_cast<std::uint32_t>(out.size()));
        put32(out, kLocalSig);
        put16(out, kVersionNeeded);
        put16(out, 0); // general purpose flags
        put16(out, b.method);
        put16(out, kDosEpochTime);
        put16(out, kDosEpochDate);
        put32(out, b.crc32);
        put32(out, static_cast<std::uint32_t>(b.stored.size()));
        put32(out, b.uncompressed_size);
        put16(out, static_cast<std::uint16_t>(b.name.size()));
        put16(out, 0); // extra field length
        out += b.name;
        out += b.stored;
    }

    const auto cd_offset = static_cast<std::uint32_t>(out.size());
    for (std::size_t i = 0; i < blobs.size(); ++i)
    {
        const Blob &b = blobs[i];
        put32(out, kCentralSig);
        put16(out, kVersionNeeded); // version made by
        put16(out, kVersionNeeded); // version needed
        put16(out, 0);              // general purpose flags
        put16(out, b.method);
        put16(out, kDosEpochTime);
        put16(out, kDosEpochDate);
        put32(out, b.crc32);
        put32(out, static_cast<std::uint32_t>(b.stored.size()));
        put32(out, b.uncompressed_size);
        put16(out, static_cast<std::uint16_t>(b.name.size()));
        put16(out, 0); // extra field length
        put16(out, 0); // comment length
        put16(out, 0); // disk number start
        put16(out, 0); // internal attributes
        put32(out, 0); // external attributes
        put32(out, offsets[i]);
        out += b.name;
    }
    const auto cd_size = static_cast<std::uint32_t>(out.size()) - cd_offset;

    put32(out, kEocdSig);
    put16(out, 0); // this disk
    put16(out, 0); // disk with the central directory
    put16(out, static_cast<std::uint16_t>(blobs.size()));
    put16(out, static_cast<std::uint16_t>(blobs.size()));
    put32(out, cd_size);
    put32(out, cd_offset);
    put16(out, 0); // archive comment length
    return out;
}

std::optional<std::vector<Entry>> read(std::string_view image)
{
    if (image.size() < kEocdSize)
        return std::nullopt;

    // Scan back for the EOCD: a trailing archive comment would push it off the end.
    std::size_t eocd = std::string_view::npos;
    const std::size_t limit = image.size() >= 0xFFFF + kEocdSize ? image.size() - (0xFFFF + kEocdSize) : 0;
    for (std::size_t i = image.size() - kEocdSize + 1; i-- > limit;)
    {
        if (get32(image, i) == kEocdSig)
        {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string_view::npos)
        return std::nullopt;

    if (get16(image, eocd + 4) != 0 || get16(image, eocd + 6) != 0)
        return std::nullopt; // multi-disk
    const std::uint16_t count = get16(image, eocd + 10);
    if (count == 0xFFFF)
        return std::nullopt; // zip64
    const std::uint32_t cd_size = get32(image, eocd + 12);
    const std::uint32_t cd_offset = get32(image, eocd + 16);
    if (cd_offset == 0xFFFFFFFFu || cd_size == 0xFFFFFFFFu)
        return std::nullopt; // zip64
    if (static_cast<std::size_t>(cd_offset) + cd_size > image.size())
        return std::nullopt;

    std::vector<Entry> entries;
    entries.reserve(count);
    std::size_t p = cd_offset;

    for (std::uint16_t i = 0; i < count; ++i)
    {
        if (p + kCentralHeaderSize > image.size() || get32(image, p) != kCentralSig)
            return std::nullopt;

        const std::uint16_t flags = get16(image, p + 8);
        if ((flags & 0x0001) != 0)
            return std::nullopt; // encrypted
        const std::uint16_t method = get16(image, p + 10);
        const std::uint32_t crc = get32(image, p + 16);
        const std::uint32_t comp_size = get32(image, p + 20);
        const std::uint32_t raw_size = get32(image, p + 24);
        const std::uint16_t name_len = get16(image, p + 28);
        const std::uint16_t extra_len = get16(image, p + 30);
        const std::uint16_t comment_len = get16(image, p + 32);
        const std::uint32_t local_off = get32(image, p + 42);

        if (comp_size == 0xFFFFFFFFu || raw_size == 0xFFFFFFFFu || local_off == 0xFFFFFFFFu)
            return std::nullopt; // zip64
        if (p + kCentralHeaderSize + name_len > image.size())
            return std::nullopt;

        const std::string_view name = image.substr(p + kCentralHeaderSize, name_len);
        if (!name_is_safe(name))
            return std::nullopt;

        if (static_cast<std::size_t>(local_off) + kLocalHeaderSize > image.size() || get32(image, local_off) != kLocalSig)
            return std::nullopt;
        const std::uint16_t l_name_len = get16(image, local_off + 26);
        const std::uint16_t l_extra_len = get16(image, local_off + 28);
        const std::size_t data_off = static_cast<std::size_t>(local_off) + kLocalHeaderSize + l_name_len + l_extra_len;
        if (data_off + comp_size > image.size())
            return std::nullopt;

        const std::string_view stored = image.substr(data_off, comp_size);
        Entry entry;
        entry.name = std::string(name);
        if (method == kStore)
        {
            if (comp_size != raw_size)
                return std::nullopt;
            entry.data = std::string(stored);
        }
        else if (method == kDeflate)
        {
            if (!inflate_raw(stored, raw_size, entry.data))
                return std::nullopt;
        }
        else
        {
            return std::nullopt;
        }

        if (crc_of(entry.data) != crc)
            return std::nullopt;

        entries.push_back(std::move(entry));
        p += kCentralHeaderSize + name_len + extra_len + comment_len;
    }

    return entries;
}

} // namespace mth::zip
