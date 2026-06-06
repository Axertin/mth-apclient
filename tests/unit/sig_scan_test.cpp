#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/sig_scan.hpp"

using mth::sig::Entry;
using mth::sig::find_masked;
using mth::sig::Kind;
using mth::sig::Match;
using mth::sig::resolve;

namespace
{
// 1-bytes for every position: exact match required.
std::vector<std::uint8_t> all_ones(std::size_t n)
{
    return std::vector<std::uint8_t>(n, 1);
}
} // namespace

TEST_CASE("find_masked locates an exact pattern", "[sig]")
{
    std::vector<std::uint8_t> region{0x00, 0x11, 0x48, 0x89, 0xE5, 0x22};
    std::vector<std::uint8_t> pat{0x48, 0x89, 0xE5};
    auto mask = all_ones(3);
    Match m = find_masked(region, pat.data(), mask.data(), pat.size());
    REQUIRE(m.found);
    REQUIRE(m.unique);
    REQUIRE(m.offset == 2);
}

TEST_CASE("find_masked ignores wildcard bytes", "[sig]")
{
    std::vector<std::uint8_t> region{0x48, 0x8B, 0x05, 0xDE, 0xAD, 0xBE, 0xEF, 0x90};
    std::vector<std::uint8_t> pat{0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00};
    std::vector<std::uint8_t> mask{1, 1, 1, 0, 0, 0, 0};
    Match m = find_masked(region, pat.data(), mask.data(), pat.size());
    REQUIRE(m.found);
    REQUIRE(m.unique);
    REQUIRE(m.offset == 0);
}

TEST_CASE("find_masked flags a non-unique pattern", "[sig]")
{
    std::vector<std::uint8_t> region{0x90, 0x90, 0x90, 0x90};
    std::vector<std::uint8_t> pat{0x90, 0x90};
    auto mask = all_ones(2);
    Match m = find_masked(region, pat.data(), mask.data(), pat.size());
    REQUIRE(m.found);
    REQUIRE_FALSE(m.unique);
}

TEST_CASE("find_masked reports a miss", "[sig]")
{
    std::vector<std::uint8_t> region{0x01, 0x02, 0x03};
    std::vector<std::uint8_t> pat{0xAA, 0xBB};
    auto mask = all_ones(2);
    Match m = find_masked(region, pat.data(), mask.data(), pat.size());
    REQUIRE_FALSE(m.found);
}

TEST_CASE("resolve returns the match address for a Code entry", "[sig]")
{
    std::vector<std::uint8_t> region{0x00, 0x55, 0x48, 0x89, 0xE5};
    std::vector<std::uint8_t> pat{0x55, 0x48, 0x89, 0xE5};
    auto mask = all_ones(4);
    Entry e{"f", Kind::Code, pat.data(), mask.data(), pat.size(), 0, 0};
    // region[0] maps to runtime 0x140001000; match begins at offset 1.
    REQUIRE(resolve(region, 0x140001000ull, e) == 0x140001001ull);
}

TEST_CASE("resolve extracts a RIP-relative global for a DataRef entry", "[sig]")
{
    // At offset 4: lea rax,[rip+0x10]  -> 48 8D 05 10 00 00 00 (7 bytes)
    std::vector<std::uint8_t> region{0x00, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x05, 0x10, 0x00, 0x00, 0x00, 0x90};
    std::vector<std::uint8_t> pat{0x48, 0x8D, 0x05, 0x00, 0x00, 0x00, 0x00};
    std::vector<std::uint8_t> mask{1, 1, 1, 0, 0, 0, 0};
    Entry e{"g", Kind::DataRef, pat.data(), mask.data(), pat.size(), /*disp_off=*/3, /*next_insn=*/7};
    // region_base 0x140001000; match offset 4; next-insn runtime addr = 0x140001000+4+7 = 0x14000100B; +0x10
    REQUIRE(resolve(region, 0x140001000ull, e) == 0x14000101Bull);
}

TEST_CASE("resolve returns 0 for an ambiguous match", "[sig]")
{
    std::vector<std::uint8_t> region{0xC3, 0xC3};
    std::vector<std::uint8_t> pat{0xC3};
    auto mask = all_ones(1);
    Entry e{"h", Kind::Code, pat.data(), mask.data(), pat.size(), 0, 0};
    REQUIRE(resolve(region, 0x1000, e) == 0);
}

TEST_CASE("resolve handles a negative RIP-relative displacement", "[sig]")
{
    // At offset 4: lea rax,[rip-0x10] -> 48 8D 05 F0 FF FF FF (disp32 = -16)
    std::vector<std::uint8_t> region{0x00, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x05, 0xF0, 0xFF, 0xFF, 0xFF, 0x90};
    std::vector<std::uint8_t> pat{0x48, 0x8D, 0x05, 0x00, 0x00, 0x00, 0x00};
    std::vector<std::uint8_t> mask{1, 1, 1, 0, 0, 0, 0};
    Entry e{"g", Kind::DataRef, pat.data(), mask.data(), pat.size(), /*disp_off=*/3, /*next_insn=*/7};
    // next-insn runtime addr = 0x140001000+4+7 = 0x14000100B; + (-0x10) = 0x140000FFB (global below the ref-site)
    REQUIRE(resolve(region, 0x140001000ull, e) == 0x140000FFBull);
}

TEST_CASE("resolve returns 0 when a DataRef disp32 runs past the region", "[sig]")
{
    // Pattern (len 3) matches, but disp_off=3 puts the 4-byte read at [3,7) in a 3-byte region.
    std::vector<std::uint8_t> region{0x48, 0x8D, 0x05};
    std::vector<std::uint8_t> pat{0x48, 0x8D, 0x05};
    auto mask = all_ones(3);
    Entry e{"oob", Kind::DataRef, pat.data(), mask.data(), pat.size(), /*disp_off=*/3, /*next_insn=*/7};
    REQUIRE(resolve(region, 0x140001000ull, e) == 0);
}

TEST_CASE("find_masked matches at the last valid offset", "[sig]")
{
    std::vector<std::uint8_t> region{0x00, 0x11, 0x55, 0x48, 0x89, 0xE5};
    std::vector<std::uint8_t> pat{0x48, 0x89, 0xE5};
    auto mask = all_ones(3);
    Match m = find_masked(region, pat.data(), mask.data(), pat.size());
    REQUIRE(m.found);
    REQUIRE(m.unique);
    REQUIRE(m.offset == 3); // region.size() - len == 6 - 3
    Entry e{"tail", Kind::Code, pat.data(), mask.data(), pat.size(), 0, 0};
    REQUIRE(resolve(region, 0x140001000ull, e) == 0x140001003ull);
}

TEST_CASE("find_masked reports a miss when the pattern is longer than the region", "[sig]")
{
    std::vector<std::uint8_t> region{0x48, 0x89};
    std::vector<std::uint8_t> pat{0x48, 0x89, 0xE5};
    auto mask = all_ones(3);
    Match m = find_masked(region, pat.data(), mask.data(), pat.size());
    REQUIRE_FALSE(m.found);
}
