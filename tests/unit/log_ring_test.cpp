#include <catch2/catch_test_macros.hpp>

#include "mth/core/log_ring.hpp"

TEST_CASE("LogRing keeps lines oldest-first until capacity", "[mth][logring]")
{
    mth::LogRing ring(3);
    ring.push("a");
    ring.push("b");
    auto s = ring.snapshot();
    REQUIRE(s == std::vector<std::string>{"a", "b"});
    REQUIRE(ring.size() == 2);
}

TEST_CASE("LogRing drops the oldest line when full", "[mth][logring]")
{
    mth::LogRing ring(3);
    ring.push("a");
    ring.push("b");
    ring.push("c");
    ring.push("d");
    REQUIRE(ring.snapshot() == std::vector<std::string>{"b", "c", "d"});
    REQUIRE(ring.size() == 3);
}

TEST_CASE("LogRing wraps correctly across more than one cycle", "[mth][logring]")
{
    mth::LogRing ring(3);
    for (const char *s : {"a", "b", "c", "d", "e"})
        ring.push(s);
    REQUIRE(ring.snapshot() == std::vector<std::string>{"c", "d", "e"});
    REQUIRE(ring.size() == 3);
}

TEST_CASE("LogRing clamps zero capacity to one", "[mth][logring]")
{
    mth::LogRing ring(0);
    ring.push("a");
    ring.push("b");
    REQUIRE(ring.snapshot() == std::vector<std::string>{"b"});
    REQUIRE(ring.size() == 1);
}

TEST_CASE("LogRing clear empties it", "[mth][logring]")
{
    mth::LogRing ring(3);
    ring.push("a");
    ring.clear();
    REQUIRE(ring.size() == 0);
    REQUIRE(ring.snapshot().empty());
}
