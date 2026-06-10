#include <catch2/catch_test_macros.hpp>

#include "mth/core/modifier_config.hpp"

TEST_CASE("parse_modifier_indices reads a comma list", "[modifiers]")
{
    const auto r = mth::parse_modifier_indices("122, 214,217");
    REQUIRE(r.indices == std::vector<int>{122, 214, 217});
    REQUIRE(r.forced.empty());
}

TEST_CASE("parse_modifier_indices drops out-of-range and junk", "[modifiers]")
{
    const auto r = mth::parse_modifier_indices("122,999,-3,abc,7abc,7");
    REQUIRE(r.indices == std::vector<int>{122, 7}); // 0..253 only; trailing-junk "7abc" dropped
}

TEST_CASE("parse_modifier_indices dedups, keeps first-seen order", "[modifiers]")
{
    const auto r = mth::parse_modifier_indices("7,7,3,7");
    REQUIRE(r.indices == std::vector<int>{7, 3});
}

TEST_CASE("parse_modifier_indices parses force: prefix", "[modifiers]")
{
    const auto r = mth::parse_modifier_indices("31, force:140, 214");
    REQUIRE(r.indices == std::vector<int>{31, 140, 214});
    REQUIRE(r.forced == std::set<int>{140});
}

TEST_CASE("parse_modifier_indices force: on a duplicate still marks it forced", "[modifiers]")
{
    const auto r = mth::parse_modifier_indices("7, force:7");
    REQUIRE(r.indices == std::vector<int>{7}); // deduped
    REQUIRE(r.forced == std::set<int>{7});     // forced flag still set
}

TEST_CASE("parse_modifier_indices drops out-of-range force: tokens", "[modifiers]")
{
    const auto r = mth::parse_modifier_indices("force:999");
    REQUIRE(r.indices.empty());
    REQUIRE(r.forced.empty()); // forced is always a subset of the valid range
}

TEST_CASE("parse_modifier_indices is empty for empty input", "[modifiers]")
{
    REQUIRE(mth::parse_modifier_indices("").indices.empty());
}
