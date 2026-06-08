#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/sig_scan.hpp"

TEST_CASE("read_riprel_target computes a RIP-relative global", "[sig]")
{
    // cmove r9,[rip+0x04a3a76e] at pretend addr 0x140823392 (8-byte insn).
    const std::uint8_t insn[] = {0x4c, 0x0f, 0x44, 0x0d, 0x6e, 0xa7, 0xa3, 0x04};
    // target = insn_addr + insn_len + disp32 = 0x140823392 + 8 + 0x04a3a76e = 0x14525db08
    REQUIRE(mth::sig::read_riprel_target(insn, 0x140823392, /*disp_off=*/4, /*insn_len=*/8) == 0x14525db08ULL);
}

TEST_CASE("read_riprel_target handles a negative RIP-relative displacement", "[sig]")
{
    // disp32 = -0x100 (little-endian bytes 00 ff ff ff) at disp_off 4 of an 8-byte insn.
    const std::uint8_t insn[] = {0x4c, 0x0f, 0x44, 0x0d, 0x00, 0xff, 0xff, 0xff};
    // target = insn_addr + insn_len + disp = 0x140001000 + 8 - 0x100 = 0x140000F08
    REQUIRE(mth::sig::read_riprel_target(insn, 0x140001000, /*disp_off=*/4, /*insn_len=*/8) == 0x140000F08ULL);
}

TEST_CASE("find_riprel_load returns 0 when no opcode matches", "[sig]")
{
    std::uint8_t buf[32] = {}; // all zeros: opcode bytes never appear
    const std::uint8_t op[] = {0x4c, 0x0f, 0x44, 0x0d};
    REQUIRE(mth::sig::find_riprel_load({buf, sizeof(buf)}, 0x1000, op, sizeof(op), /*disp_off=*/4, /*insn_len=*/8) == 0ULL);

    // Region smaller than insn_len -> 0.
    std::uint8_t small[4] = {0x4c, 0x0f, 0x44, 0x0d};
    REQUIRE(mth::sig::find_riprel_load({small, sizeof(small)}, 0x1000, op, sizeof(op), /*disp_off=*/4, /*insn_len=*/8) == 0ULL);
}

TEST_CASE("find_riprel_load locates the first matching opcode in a window", "[sig]")
{
    std::uint8_t buf[32] = {};
    buf[0] = 0x55; // push rbp (filler)
    buf[1] = 0x4c;
    buf[2] = 0x0f;
    buf[3] = 0x44;
    buf[4] = 0x0d; // cmove r9,[rip+..] at offset 1
    buf[5] = 0x00;
    buf[6] = 0x10;
    buf[7] = 0x00;
    buf[8] = 0x00; // disp32 = 0x1000
    const std::uint8_t op[] = {0x4c, 0x0f, 0x44, 0x0d};
    // base addr 0x1000; match at off 1 -> insn_addr 0x1001; target 0x1001 + 8 + 0x1000 = 0x2009
    REQUIRE(mth::sig::find_riprel_load({buf, sizeof(buf)}, 0x1000, op, sizeof(op), /*disp_off=*/4, /*insn_len=*/8) == 0x2009ULL);
}
