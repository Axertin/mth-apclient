#include <catch2/catch_test_macros.hpp>

#include "mocks/fake_ap_link.hpp"
#include "mth/core/ap_state.hpp"
#include "mth/core/rando_bridge.hpp"

using mth::ap_loc_id;

namespace
{
mth::ApState connected_with(std::vector<std::int64_t> missing)
{
    mth::ApState s;
    s.apply(mth::ApConnected{{}, "{}", 1, {}, std::move(missing)});
    return s;
}
} // namespace

TEST_CASE("rando_bridge: valid location is sent once", "[mth][rando]")
{
    mth::test::FakeApLink link;
    auto state = connected_with({ap_loc_id(5), ap_loc_id(6)});
    mth::RandoBridge bridge(link, state);

    bridge.on_location_collected(5);
    REQUIRE(link.sent_locations == std::vector<std::int64_t>{ap_loc_id(5)});

    bridge.on_location_collected(5); // duplicate -> not resent
    REQUIRE(link.sent_locations.size() == 1);
}

TEST_CASE("rando_bridge: unknown location is dropped", "[mth][rando]")
{
    mth::test::FakeApLink link;
    auto state = connected_with({ap_loc_id(5)});
    mth::RandoBridge bridge(link, state);

    bridge.on_location_collected(7); // not in missing/checked
    REQUIRE(link.sent_locations.empty());
}

TEST_CASE("rando_bridge: negative slot is ignored", "[mth][rando]")
{
    mth::test::FakeApLink link;
    auto state = connected_with({ap_loc_id(0)});
    mth::RandoBridge bridge(link, state);

    bridge.on_location_collected(-1); // non-location pickup (e.g. enemy drop)
    REQUIRE(link.sent_locations.empty());
}
