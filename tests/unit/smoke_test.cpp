#include <catch2/catch_test_macros.hpp>

TEST_CASE("smoke: Catch2 reaches main", "[smoke]")
{
    REQUIRE(1 + 1 == 2);
}
