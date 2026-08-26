#include <array>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/data/cheat_mask.hpp"

namespace
{
// One index touching each end of each of the four 64-bit words, plus 255, the top of the range the
// mask's four words can address (the real cheat count stops at 253; 254/255 are still in-bounds for
// this header's own contract).
constexpr std::array<int, 9> kBoundaryIndices = {0, 63, 64, 127, 128, 191, 192, 253, 255};
} // namespace

TEST_CASE("cheat_mask: word selection lands in the right 64-bit word", "[cheat_mask]")
{
    REQUIRE(mth::cheat_mask_word(0) == 0);
    REQUIRE(mth::cheat_mask_word(63) == 0);
    REQUIRE(mth::cheat_mask_word(64) == 1);
    REQUIRE(mth::cheat_mask_word(127) == 1);
    REQUIRE(mth::cheat_mask_word(128) == 2);
    REQUIRE(mth::cheat_mask_word(191) == 2);
    REQUIRE(mth::cheat_mask_word(192) == 3);
    REQUIRE(mth::cheat_mask_word(253) == 3);
    REQUIRE(mth::cheat_mask_word(255) == 3);
}

TEST_CASE("cheat_mask: bit selection lands on the right bit within its word", "[cheat_mask]")
{
    REQUIRE(mth::cheat_mask_bit(0) == std::uint64_t{1} << 0);
    REQUIRE(mth::cheat_mask_bit(63) == std::uint64_t{1} << 63);
    REQUIRE(mth::cheat_mask_bit(64) == std::uint64_t{1} << 0);
    REQUIRE(mth::cheat_mask_bit(127) == std::uint64_t{1} << 63);
    REQUIRE(mth::cheat_mask_bit(128) == std::uint64_t{1} << 0);
    REQUIRE(mth::cheat_mask_bit(191) == std::uint64_t{1} << 63);
    REQUIRE(mth::cheat_mask_bit(192) == std::uint64_t{1} << 0);
    REQUIRE(mth::cheat_mask_bit(253) == std::uint64_t{1} << 61);
    REQUIRE(mth::cheat_mask_bit(255) == std::uint64_t{1} << 63);
}

TEST_CASE("cheat_mask: setting one index touches only that bit, in only that word", "[cheat_mask]")
{
    for (int idx : kBoundaryIndices)
    {
        std::uint64_t mask[4] = {0, 0, 0, 0};
        mth::cheat_mask_set(mask, idx, true);

        const int w = mth::cheat_mask_word(idx);
        REQUIRE(mask[w] == mth::cheat_mask_bit(idx));
        for (int other = 0; other < 4; ++other)
        {
            if (other != w)
                REQUIRE(mask[other] == 0);
        }
        REQUIRE(mth::cheat_mask_test(mask, idx));

        // Adjacent indices (which may share a word but never a bit) must read false. idx-1/idx+1
        // stay within 0..255 here on purpose; the dedicated out-of-range tests below cover -1/256.
        if (idx > 0)
            REQUIRE_FALSE(mth::cheat_mask_test(mask, idx - 1));
        if (idx < 255)
            REQUIRE_FALSE(mth::cheat_mask_test(mask, idx + 1));
    }
}

TEST_CASE("cheat_mask: clearing zeroes only the target bit", "[cheat_mask]")
{
    std::uint64_t mask[4] = {~std::uint64_t{0}, ~std::uint64_t{0}, ~std::uint64_t{0}, ~std::uint64_t{0}};
    for (int idx : kBoundaryIndices)
        mth::cheat_mask_set(mask, idx, false);

    for (int idx = 0; idx < 256; ++idx)
    {
        bool was_cleared = false;
        for (int b : kBoundaryIndices)
            was_cleared = was_cleared || (b == idx);
        REQUIRE(mth::cheat_mask_test(mask, idx) == !was_cleared);
    }
}

TEST_CASE("cheat_mask: set then clear round-trips to zero", "[cheat_mask]")
{
    for (int idx : kBoundaryIndices)
    {
        std::uint64_t mask[4] = {0, 0, 0, 0};
        mth::cheat_mask_set(mask, idx, true);
        mth::cheat_mask_set(mask, idx, false);
        for (int w = 0; w < 4; ++w)
            REQUIRE(mask[w] == 0);
    }
}

TEST_CASE("cheat_mask: an out-of-range index leaves cheat_mask_set a total no-op", "[cheat_mask]")
{
    // The four mask words sit inside a six-word buffer with a guard word on each side, because idx=-1
    // writes one word below the mask and idx=256 one word above it: a four-word array would put both
    // of those outside the object entirely and every assertion here would pass regardless. Distinct
    // per-word sentinels also catch a write that lands on the wrong word inside the mask.
    for (int idx : {-1, 256})
    {
        std::uint64_t buf[6] = {0x5555555555555555ull, 0x1111111111111111ull, 0x2222222222222222ull,
                                0x3333333333333333ull, 0x4444444444444444ull, 0xaaaaaaaaaaaaaaaaull};
        std::uint64_t *mask = &buf[1];
        mth::cheat_mask_set(mask, idx, true);
        REQUIRE(buf[0] == 0x5555555555555555ull);
        REQUIRE(mask[0] == 0x1111111111111111ull);
        REQUIRE(mask[1] == 0x2222222222222222ull);
        REQUIRE(mask[2] == 0x3333333333333333ull);
        REQUIRE(mask[3] == 0x4444444444444444ull);
        REQUIRE(buf[5] == 0xaaaaaaaaaaaaaaaaull);

        mth::cheat_mask_set(mask, idx, false);
        REQUIRE(buf[0] == 0x5555555555555555ull);
        REQUIRE(mask[0] == 0x1111111111111111ull);
        REQUIRE(mask[1] == 0x2222222222222222ull);
        REQUIRE(mask[2] == 0x3333333333333333ull);
        REQUIRE(mask[3] == 0x4444444444444444ull);
        REQUIRE(buf[5] == 0xaaaaaaaaaaaaaaaaull);
    }
}

TEST_CASE("cheat_mask: an out-of-range index reads false from cheat_mask_test", "[cheat_mask]")
{
    // Same six-word shape, all bits set, so an unguarded read of buf[0] or buf[5] returns true and
    // the assertions below fail rather than reading whatever happened to sit past a four-word array.
    const std::uint64_t buf[6] = {~std::uint64_t{0}, ~std::uint64_t{0}, ~std::uint64_t{0}, ~std::uint64_t{0}, ~std::uint64_t{0}, ~std::uint64_t{0}};
    const std::uint64_t *mask = &buf[1];
    REQUIRE_FALSE(mth::cheat_mask_test(mask, -1));
    REQUIRE_FALSE(mth::cheat_mask_test(mask, 256));
}
