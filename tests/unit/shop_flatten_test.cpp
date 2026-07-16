#include <catch2/catch_test_macros.hpp>

#include "mth/core/shop_flatten.hpp"

TEST_CASE("apply_flatten_flag: sets the never-stack bit when active", "[shop_flatten]")
{
    REQUIRE(mth::apply_flatten_flag(0u, true) == mth::kShopNeverStackBit);
}

TEST_CASE("apply_flatten_flag: leaves flags untouched when inactive", "[shop_flatten]")
{
    REQUIRE(mth::apply_flatten_flag(0u, false) == 0u);
    REQUIRE(mth::apply_flatten_flag(0x55u, false) == 0x55u);
}

TEST_CASE("apply_flatten_flag: preserves other bits and is idempotent", "[shop_flatten]")
{
    const std::uint32_t base = 0x2Au; // some unrelated authored flags
    const std::uint32_t once = mth::apply_flatten_flag(base, true);
    REQUIRE(once == (base | mth::kShopNeverStackBit));
    REQUIRE(mth::apply_flatten_flag(once, true) == once); // OR is idempotent
}

TEST_CASE("apply_flatten_flag: leaves an already-flat shop flat when active", "[shop_flatten]")
{
    REQUIRE(mth::apply_flatten_flag(mth::kShopNeverStackBit, true) == mth::kShopNeverStackBit);
}
