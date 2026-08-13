#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/save/zip_archive.hpp"

namespace
{
std::uint16_t u16at(const std::string &s, std::size_t off)
{
    return static_cast<std::uint16_t>(static_cast<unsigned char>(s[off]) | (static_cast<unsigned char>(s[off + 1]) << 8));
}

std::uint32_t u32at(const std::string &s, std::size_t off)
{
    return static_cast<std::uint32_t>(static_cast<unsigned char>(s[off])) | (static_cast<std::uint32_t>(static_cast<unsigned char>(s[off + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[off + 2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[off + 3])) << 24);
}
} // namespace

TEST_CASE("zip round-trips a single entry", "[zip]")
{
    const std::string body = "[YCD Version: 1]\nSaveSlot\n{ m_iFoo: 1 }";
    const std::string image = mth::zip::write({mth::zip::compress("save.ycsave", body)});

    const auto entries = mth::zip::read(image);
    REQUIRE(entries.has_value());
    REQUIRE(entries->size() == 1);
    REQUIRE((*entries)[0].name == "save.ycsave");
    REQUIRE((*entries)[0].data == body);
}

TEST_CASE("zip round-trips several entries and preserves order", "[zip]")
{
    const std::vector<mth::zip::Blob> blobs{mth::zip::compress("manifest.json", "{\"format\":1}"), mth::zip::compress("save.ycsave", "payload"),
                                            mth::zip::compress("ap.state", "c 1\ng 2\n")};
    const auto entries = mth::zip::read(mth::zip::write(blobs));
    REQUIRE(entries.has_value());
    REQUIRE(entries->size() == 3);
    REQUIRE((*entries)[0].name == "manifest.json");
    REQUIRE((*entries)[1].name == "save.ycsave");
    REQUIRE((*entries)[2].name == "ap.state");
    REQUIRE((*entries)[1].data == "payload");
}

TEST_CASE("zip round-trips binary payloads including embedded NULs", "[zip]")
{
    std::string body;
    for (int i = 0; i < 512; ++i)
        body.push_back(static_cast<char>(i & 0xFF));
    const auto entries = mth::zip::read(mth::zip::write({mth::zip::compress("bin", body)}));
    REQUIRE(entries.has_value());
    REQUIRE((*entries)[0].data == body);
}

TEST_CASE("zip round-trips an empty payload", "[zip]")
{
    const auto entries = mth::zip::read(mth::zip::write({mth::zip::compress("empty", "")}));
    REQUIRE(entries.has_value());
    REQUIRE(entries->size() == 1);
    REQUIRE((*entries)[0].data.empty());
}

TEST_CASE("compressible text actually deflates", "[zip]")
{
    const std::string body(4096, 'a');
    const auto blob = mth::zip::compress("save.ycsave", body);
    REQUIRE(blob.method == mth::zip::kDeflate);
    REQUIRE(blob.stored.size() < body.size());
    REQUIRE(blob.uncompressed_size == body.size());
}

TEST_CASE("incompressible input falls back to store", "[zip]")
{
    // A short pseudo-random run deflates larger than it started; the writer must not grow the file.
    std::string body;
    std::uint32_t x = 123456789;
    for (int i = 0; i < 64; ++i)
    {
        x = x * 1664525u + 1013904223u;
        body.push_back(static_cast<char>(x >> 24));
    }
    const auto blob = mth::zip::compress("noise", body);
    REQUIRE(blob.stored.size() <= body.size());
    if (blob.stored.size() == body.size())
        REQUIRE(blob.method == mth::zip::kStore);
    const auto entries = mth::zip::read(mth::zip::write({blob}));
    REQUIRE(entries.has_value());
    REQUIRE((*entries)[0].data == body);
}

TEST_CASE("zip output is byte-deterministic", "[zip]")
{
    const std::string a = mth::zip::write({mth::zip::compress("x", "hello world")});
    const std::string b = mth::zip::write({mth::zip::compress("x", "hello world")});
    REQUIRE(a == b);
}

TEST_CASE("zip header layout matches the standard", "[zip]")
{
    const std::string image = mth::zip::write({mth::zip::compress("ap.state", "c 1\n")});

    // Local file header.
    REQUIRE(u32at(image, 0) == 0x04034b50u);
    REQUIRE(u16at(image, 4) == 20);    // version needed
    REQUIRE(u16at(image, 6) == 0);     // general purpose flags
    REQUIRE(u16at(image, 10) == 0);    // dos time
    REQUIRE(u16at(image, 12) == 0x21); // dos date = 1980-01-01
    REQUIRE(u16at(image, 26) == 8);    // name length
    REQUIRE(u16at(image, 28) == 0);    // extra length
    REQUIRE(image.substr(30, 8) == "ap.state");

    // EOCD is the final 22 bytes (no archive comment).
    const std::size_t eocd = image.size() - 22;
    REQUIRE(u32at(image, eocd) == 0x06054b50u);
    REQUIRE(u16at(image, eocd + 8) == 1);  // records on this disk
    REQUIRE(u16at(image, eocd + 10) == 1); // total records
    const std::uint32_t cd_size = u32at(image, eocd + 12);
    const std::uint32_t cd_off = u32at(image, eocd + 16);
    REQUIRE(cd_off + cd_size == eocd);
    REQUIRE(u32at(image, cd_off) == 0x02014b50u);
}

TEST_CASE("zip rejects malformed images", "[zip]")
{
    const std::string good = mth::zip::write({mth::zip::compress("ap.state", "c 1\n")});

    REQUIRE_FALSE(mth::zip::read("").has_value());
    REQUIRE_FALSE(mth::zip::read("not a zip at all").has_value());
    REQUIRE_FALSE(mth::zip::read(good.substr(0, good.size() - 4)).has_value()); // truncated EOCD
    REQUIRE_FALSE(mth::zip::read(good.substr(0, good.size() / 2)).has_value()); // truncated body
}

TEST_CASE("zip rejects a corrupted payload via CRC", "[zip]")
{
    std::string image = mth::zip::write({mth::zip::compress("ap.state", std::string(2048, 'q'))});
    // Flip a byte inside the compressed data, past the 30-byte header and the 8-byte name.
    image[40] = static_cast<char>(image[40] ^ 0xFF);
    REQUIRE_FALSE(mth::zip::read(image).has_value());
}

TEST_CASE("zip rejects unsafe entry names on read", "[zip]")
{
    for (const char *name : {"../escape", "/abs/path", "a/../../b", "C:\\win"})
    {
        auto blob = mth::zip::compress("placeholder", "x");
        blob.name = name; // bypass the writer; simulate a hostile container
        REQUIRE_FALSE(mth::zip::read(mth::zip::write({blob})).has_value());
    }
}

TEST_CASE("zip rejects an implausible uncompressed size without allocating it", "[zip]")
{
    std::string image = mth::zip::write({mth::zip::compress("ap.state", "c 1\n")});
    const std::size_t eocd = image.size() - 22;
    const std::uint32_t cd_off = u32at(image, eocd + 16);

    // Claim 2 GB uncompressed in the central directory. The reader must reject on the header rather
    // than resize a buffer to it.
    const std::size_t raw_at = cd_off + 24;
    image[raw_at + 0] = static_cast<char>(0x00);
    image[raw_at + 1] = static_cast<char>(0x00);
    image[raw_at + 2] = static_cast<char>(0x00);
    image[raw_at + 3] = static_cast<char>(0x7F);
    REQUIRE_FALSE(mth::zip::read(image).has_value());
}

TEST_CASE("zip accepts nested-looking but safe names", "[zip]")
{
    auto blob = mth::zip::compress("sub/dir/file.txt", "x");
    const auto entries = mth::zip::read(mth::zip::write({blob}));
    REQUIRE(entries.has_value());
    REQUIRE((*entries)[0].name == "sub/dir/file.txt");
}
