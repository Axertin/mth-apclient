#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mth::hookhash
{

// Jenkins lookup3 hashlittle2 with the loader's seeds (pc=0x0d, pb=0x25), returning pc | (pb << 32).
// RunHooks dispatches on this, precomputed and inlined at each site as a movabs immediate. It
// derives from the hook name rather than surrounding code, so it anchors the enclosing function.

constexpr std::uint32_t rot(std::uint32_t x, int k) noexcept
{
    return (x << k) | (x >> (32 - k));
}

struct Abc
{
    std::uint32_t a, b, c;
};

constexpr Abc mix(Abc v) noexcept
{
    v.a -= v.c;
    v.a ^= rot(v.c, 4);
    v.c += v.b;
    v.b -= v.a;
    v.b ^= rot(v.a, 6);
    v.a += v.c;
    v.c -= v.b;
    v.c ^= rot(v.b, 8);
    v.b += v.a;
    v.a -= v.c;
    v.a ^= rot(v.c, 16);
    v.c += v.b;
    v.b -= v.a;
    v.b ^= rot(v.a, 19);
    v.a += v.c;
    v.c -= v.b;
    v.c ^= rot(v.b, 4);
    v.b += v.a;
    return v;
}

constexpr Abc final_mix(Abc v) noexcept
{
    v.c ^= v.b;
    v.c -= rot(v.b, 14);
    v.a ^= v.c;
    v.a -= rot(v.c, 11);
    v.b ^= v.a;
    v.b -= rot(v.a, 25);
    v.c ^= v.b;
    v.c -= rot(v.b, 16);
    v.a ^= v.c;
    v.a -= rot(v.c, 4);
    v.b ^= v.a;
    v.b -= rot(v.a, 14);
    v.c ^= v.b;
    v.c -= rot(v.b, 24);
    return v;
}

constexpr std::uint32_t word_at(std::string_view s, std::size_t i) noexcept
{
    return static_cast<std::uint32_t>(static_cast<unsigned char>(s[i])) | (static_cast<std::uint32_t>(static_cast<unsigned char>(s[i + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[i + 2])) << 16) | (static_cast<std::uint32_t>(static_cast<unsigned char>(s[i + 3])) << 24);
}

[[nodiscard]] constexpr std::uint64_t hash64(std::string_view name) noexcept
{
    const auto seed = static_cast<std::uint32_t>(0xdeadbeefU + name.size() + 0x0dU);
    Abc v{seed, seed, seed + 0x25U};

    std::size_t off = 0;
    std::size_t left = name.size();
    while (left > 12)
    {
        v.a += word_at(name, off);
        v.b += word_at(name, off + 4);
        v.c += word_at(name, off + 8);
        v = mix(v);
        off += 12;
        left -= 12;
    }

    // A zero-length tail requires no mixing, matching the reference implementation.
    if (left != 0)
    {
        for (std::size_t i = 0; i < left; ++i)
        {
            const auto byte = static_cast<std::uint32_t>(static_cast<unsigned char>(name[off + i]));
            if (i < 4)
                v.a += byte << (8 * i);
            else if (i < 8)
                v.b += byte << (8 * (i - 4));
            else
                v.c += byte << (8 * (i - 8));
        }
        v = final_mix(v);
    }

    return static_cast<std::uint64_t>(v.c) | (static_cast<std::uint64_t>(v.b) << 32);
}

} // namespace mth::hookhash
