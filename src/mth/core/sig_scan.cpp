#include "mth/core/sig_scan.hpp"

namespace mth::sig
{

namespace
{
bool matches_at(const std::uint8_t *base, const std::uint8_t *pattern, const std::uint8_t *mask, std::size_t len)
{
    for (std::size_t i = 0; i < len; ++i)
    {
        if (mask[i] && base[i] != pattern[i])
            return false;
    }
    return true;
}

std::int32_t read_i32le(const std::uint8_t *p)
{
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) |
                                     (static_cast<std::uint32_t>(p[3]) << 24));
}
} // namespace

Match find_masked(std::span<const std::uint8_t> region, const std::uint8_t *pattern, const std::uint8_t *mask, std::size_t len)
{
    Match result{false, false, 0};
    if (len == 0 || len > region.size())
        return result;

    const std::size_t last = region.size() - len;
    for (std::size_t i = 0; i <= last; ++i)
    {
        if (!matches_at(region.data() + i, pattern, mask, len))
            continue;
        if (!result.found)
        {
            result.found = true;
            result.unique = true;
            result.offset = i;
        }
        else
        {
            result.unique = false;
            break; // a second hit is enough to know it isn't unique
        }
    }
    return result;
}

std::uintptr_t resolve(std::span<const std::uint8_t> region, std::uintptr_t region_base, const Entry &entry)
{
    const Match m = find_masked(region, entry.pattern, entry.mask, entry.len);
    if (!m.found || !m.unique)
        return 0;

    if (entry.kind == Kind::Code)
        return region_base + m.offset;

    // DataRef: read the disp32 embedded at the match site, add to the next-insn RIP.
    const std::size_t disp_at = m.offset + static_cast<std::size_t>(entry.disp_off);
    if (disp_at + 4 > region.size())
        return 0;
    const std::int32_t disp = read_i32le(region.data() + disp_at);
    const std::uintptr_t next_ip = region_base + m.offset + static_cast<std::size_t>(entry.next_insn);
    return next_ip + static_cast<std::intptr_t>(disp);
}

} // namespace mth::sig
