#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "mocks/fake_ap_link.hpp"
#include "mth/core/area_reporter.hpp"

using mth::AreaReporter;

TEST_CASE("area_reporter: nothing sent while disconnected", "[mth][area]")
{
    mth::test::FakeApLink link;
    AreaReporter rep(link);

    rep.tick(false, std::optional<std::uint32_t>{100});
    REQUIRE(link.reported_areas.empty());
}

TEST_CASE("area_reporter: first connected state is sent once", "[mth][area]")
{
    mth::test::FakeApLink link;
    AreaReporter rep(link);

    rep.tick(true, std::optional<std::uint32_t>{100});
    rep.tick(true, std::optional<std::uint32_t>{100});
    REQUIRE(link.reported_areas == std::vector<int>{100});
}

TEST_CASE("area_reporter: only changes are sent", "[mth][area]")
{
    mth::test::FakeApLink link;
    AreaReporter rep(link);

    rep.tick(true, std::optional<std::uint32_t>{100});
    rep.tick(true, std::optional<std::uint32_t>{200});
    rep.tick(true, std::optional<std::uint32_t>{200});
    rep.tick(true, std::optional<std::uint32_t>{100});
    REQUIRE(link.reported_areas == std::vector<int>{100, 200, 100});
}

TEST_CASE("area_reporter: unreadable state sends nothing", "[mth][area]")
{
    mth::test::FakeApLink link;
    AreaReporter rep(link);

    rep.tick(true, std::nullopt);
    REQUIRE(link.reported_areas.empty());
    rep.tick(true, std::optional<std::uint32_t>{100});
    REQUIRE(link.reported_areas == std::vector<int>{100});
}

TEST_CASE("area_reporter: reconnect re-sends even if unchanged", "[mth][area]")
{
    mth::test::FakeApLink link;
    AreaReporter rep(link);

    rep.tick(true, std::optional<std::uint32_t>{100});  // send 100
    rep.tick(false, std::optional<std::uint32_t>{100}); // disconnect
    rep.tick(true, std::optional<std::uint32_t>{100});  // reconnect -> re-send 100
    REQUIRE(link.reported_areas == std::vector<int>{100, 100});
}
