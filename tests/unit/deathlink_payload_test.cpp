#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "mth/net/deathlink.hpp"

using mth::net::deathlink_cause;
using mth::net::make_deathlink_payload;
using mth::net::parse_deathlink_payload;

TEST_CASE("parse_deathlink_payload: keeps the sending slot name", "[mth][deathlink]")
{
    const auto dl = parse_deathlink_payload(R"({"time":1.5,"source":"Amaterasu","cause":"Amaterasu was crushed by a spike trap"})");

    REQUIRE(dl.has_value());
    REQUIRE(dl->source == "Amaterasu");
    REQUIRE(dl->cause == "Amaterasu was crushed by a spike trap");
}

TEST_CASE("parse_deathlink_payload: a bounce may carry no cause", "[mth][deathlink]")
{
    const auto dl = parse_deathlink_payload(R"({"time":1.5,"source":"Amaterasu"})");

    REQUIRE(dl.has_value());
    REQUIRE(dl->source == "Amaterasu");
    REQUIRE(dl->cause.empty());
}

TEST_CASE("parse_deathlink_payload: malformed payloads yield nothing", "[mth][deathlink]")
{
    REQUIRE_FALSE(parse_deathlink_payload("not json").has_value());
    REQUIRE_FALSE(parse_deathlink_payload("[1,2,3]").has_value()); // valid JSON, wrong shape
}

TEST_CASE("deathlink_cause: names the sending player, because receivers show the cause verbatim", "[mth][deathlink]")
{
    REQUIRE(deathlink_cause("Mina", "was hollowed out") == "Mina was hollowed out");
}

TEST_CASE("deathlink_cause: an unknown slot leaves the detail standing alone", "[mth][deathlink]")
{
    REQUIRE(deathlink_cause("", "was hollowed out") == "was hollowed out"); // no leading space
}

TEST_CASE("deathlink payload: source survives a round trip", "[mth][deathlink]")
{
    const auto dl = parse_deathlink_payload(make_deathlink_payload("Mina", "Mina fell in a pit", 12.0));

    REQUIRE(dl.has_value());
    REQUIRE(dl->source == "Mina");
    REQUIRE(dl->cause == "Mina fell in a pit");
}

TEST_CASE("deathlink payload: time survives with sub-second precision", "[mth][deathlink]")
{
    // Receivers dedupe on exact equality with the last timestamp they saw, so truncating to whole seconds
    // would collapse two players dying in the same second into one death.
    const auto j = nlohmann::json::parse(make_deathlink_payload("Mina", "Mina fell in a pit", 1786866611.25));

    REQUIRE(j.at("time").get<double>() == 1786866611.25);
}
