#include <catch2/catch_test_macros.hpp>

#include "pal/pal_module.hpp"

namespace
{

struct TextRangeGuard
{
    pal::TextRange saved{pal::text_range_storage()};

    ~TextRangeGuard()
    {
        pal::set_game_text_range(saved);
    }
};

} // namespace

TEST_CASE("pal: in_game_text fails open when no range is set", "[pal]")
{
    pal::set_game_text_range(pal::TextRange{});
    int marker = 0;
    REQUIRE(pal::in_game_text(&marker));
    // Null is never callable, so it is rejected even with no range published.
    REQUIRE_FALSE(pal::in_game_text(nullptr));
}

TEST_CASE("pal: in_game_text bounds-checks against the set range", "[pal]")
{
    TextRangeGuard guard;
    constexpr std::uintptr_t base = 0x400000;
    constexpr std::size_t size = 0x1000;
    pal::set_game_text_range(pal::TextRange{base, size});

    REQUIRE(pal::in_game_text(reinterpret_cast<const void *>(base)));
    REQUIRE(pal::in_game_text(reinterpret_cast<const void *>(base + size - 1)));
    REQUIRE_FALSE(pal::in_game_text(reinterpret_cast<const void *>(base - 1)));
    REQUIRE_FALSE(pal::in_game_text(reinterpret_cast<const void *>(base + size)));
    REQUIRE_FALSE(pal::in_game_text(nullptr));
}
