#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace mth::sig
{

enum class Kind
{
    Code,    // pattern marks a function; resolve to the match address
    DataRef, // pattern marks a code site referencing a global via RIP-relative disp32
};

// One signature, keyed by the same mangled name used in game_symbols.hpp.
struct Entry
{
    const char *name;
    Kind kind;
    const std::uint8_t *pattern;
    const std::uint8_t *mask; // 1 = compare this byte, 0 = wildcard
    std::size_t len;
    std::int32_t disp_off;  // DataRef only: offset of the disp32 within the match
    std::int32_t next_insn; // DataRef only: offset of the next instruction within the match
};

struct Match
{
    bool found;
    bool unique;        // false if the pattern matched more than once in the region
    std::size_t offset; // byte offset of the first match within the region (valid iff found)
};

// Scan [region] for the masked [pattern]/[mask] of [len] bytes. Stops scanning
// once a second match is seen (enough to flag non-uniqueness). A zero-length or
// over-long pattern yields {false,false,0}.
Match find_masked(std::span<const std::uint8_t> region, const std::uint8_t *pattern, const std::uint8_t *mask, std::size_t len);

// Resolve one entry against a code [region] whose first byte maps to runtime
// address [region_base]. Returns the absolute runtime address, or 0 on a miss
// or an ambiguous (non-unique) match.
//   Code    -> region_base + match_offset
//   DataRef -> (region_base + match_offset + next_insn) + read_i32le(match + disp_off)
std::uintptr_t resolve(std::span<const std::uint8_t> region, std::uintptr_t region_base, const Entry &entry);

// Read the absolute target of a RIP-relative instruction at runtime address insn_addr.
// disp_off = byte offset of the disp32 within the instruction; insn_len = total length.
[[nodiscard]] std::uintptr_t read_riprel_target(const std::uint8_t *insn, std::uintptr_t insn_addr, int disp_off, int insn_len);

// Scan up to region.size() bytes for the first instruction whose first `op_len` bytes
// equal `op`, then return its RIP-relative target. Returns 0 if not found.
[[nodiscard]] std::uintptr_t find_riprel_load(std::span<const std::uint8_t> region, std::uintptr_t region_base, const std::uint8_t *op, std::size_t op_len,
                                              int disp_off, int insn_len);

} // namespace mth::sig
