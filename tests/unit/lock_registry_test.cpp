#include <algorithm>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/lock_registry.hpp"

TEST_CASE("LockRegistry tracks removed slots", "[locks]")
{
    mth::LockRegistry reg;
    REQUIRE_FALSE(reg.is_removed(12));
    reg.set_removed(12);
    REQUIRE(reg.is_removed(12));
    REQUIRE_FALSE(reg.is_removed(13));
}

TEST_CASE("LockRegistry ignores negative/cosmetic slots", "[locks]")
{
    mth::LockRegistry reg;
    reg.set_removed(-1);
    REQUIRE_FALSE(reg.is_removed(-1));
}

TEST_CASE("LockRegistry parses a comma-separated slot list", "[locks]")
{
    mth::LockRegistry reg;
    reg.add_from_list("3, 7,  20");
    REQUIRE(reg.is_removed(3));
    REQUIRE(reg.is_removed(7));
    REQUIRE(reg.is_removed(20));
    REQUIRE_FALSE(reg.is_removed(4));
}

TEST_CASE("LockRegistry::removed_slots returns all inserted slots", "[locks]")
{
    mth::LockRegistry reg;
    reg.add_from_list("7,3,7");
    auto v = reg.removed_slots();
    std::sort(v.begin(), v.end());
    REQUIRE(v == std::vector<int>{3, 7});
}
