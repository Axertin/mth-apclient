#include <catch2/catch_test_macros.hpp>

#include "mth/net/deathlink.hpp"

using mth::net::make_deathlink_payload;
using mth::net::parse_deathlink_payload;

TEST_CASE("parse_deathlink_payload: keeps the sending slot name", "[mth][deathlink]")
{
    const auto dl = parse_deathlink_payload(R"({"time":1.5,"source":"Skylar","cause":"Skylar was crushed by a spike trap"})");

    REQUIRE(dl.has_value());
    REQUIRE(dl->source == "Skylar");
    REQUIRE(dl->cause == "Skylar was crushed by a spike trap");
}

TEST_CASE("parse_deathlink_payload: a bounce may carry no cause", "[mth][deathlink]")
{
    const auto dl = parse_deathlink_payload(R"({"time":1.5,"source":"Skylar"})");

    REQUIRE(dl.has_value());
    REQUIRE(dl->source == "Skylar");
    REQUIRE(dl->cause.empty());
}

TEST_CASE("parse_deathlink_payload: malformed payloads yield nothing", "[mth][deathlink]")
{
    REQUIRE_FALSE(parse_deathlink_payload("not json").has_value());
    REQUIRE_FALSE(parse_deathlink_payload("[1,2,3]").has_value()); // valid JSON, wrong shape
}

TEST_CASE("deathlink payload: source survives a round trip", "[mth][deathlink]")
{
    const auto dl = parse_deathlink_payload(make_deathlink_payload("Mina", "Mina fell in a pit", 12.0));

    REQUIRE(dl.has_value());
    REQUIRE(dl->source == "Mina");
    REQUIRE(dl->cause == "Mina fell in a pit");
}
