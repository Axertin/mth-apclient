#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/ap/ap_events.hpp"
#include "mth/core/ap/ap_state.hpp"
#include "mth/core/rando_bridge.hpp"
#include "mth/core/stat_cap_state.hpp"

namespace
{
// ApState is non-copyable/non-movable; populate in-place.
void make_state(mth::ApState &s, const std::vector<std::int64_t> &item_ids)
{
    int idx = 0;
    for (auto id : item_ids)
    {
        mth::ApItemReceived e;
        e.item.item_id = id;
        e.item.index = idx++; // ApState dedups on strictly-increasing index
        s.apply(e);
    }
}
} // namespace

TEST_CASE("default caps are zero (stat frozen at start)", "[stat_cap]")
{
    mth::StatCapState caps;
    REQUIRE(caps.enforced_cap(0, 9) == 0);
    REQUIRE(caps.enforced_cap(1, 9) == 0);
    REQUIRE(caps.enforced_cap(2, 9) == 0);
}

TEST_CASE("cap-up items raise only their own stat", "[stat_cap]")
{
    mth::StatCapState caps;
    mth::ApState s1;
    make_state(s1, {mth::kProgStatCapBase + 0, mth::kProgStatCapBase + 0, mth::kProgStatCapBase + 2});
    caps.recompute(s1);
    REQUIRE(caps.enforced_cap(0, 9) == 2);
    REQUIRE(caps.enforced_cap(1, 9) == 0);
    REQUIRE(caps.enforced_cap(2, 9) == 1);
    REQUIRE(caps.granted(0) == 2);
    REQUIRE(caps.granted(2) == 1);
}

TEST_CASE("all-stat cap-up raises every stat per receipt", "[stat_cap]")
{
    mth::StatCapState caps;
    mth::ApState s2;
    make_state(s2, {mth::kProgStatCapAllId, mth::kProgStatCapAllId});
    caps.recompute(s2);
    REQUIRE(caps.granted(0) == 2);
    REQUIRE(caps.granted(1) == 2);
    REQUIRE(caps.granted(2) == 2);
}

TEST_CASE("enforced cap never exceeds vanilla", "[stat_cap]")
{
    mth::StatCapState caps;
    caps.set_counts(20, 0, 0);
    REQUIRE(caps.enforced_cap(0, 9) == 9);
}

TEST_CASE("enforced cap passes through at and below vanilla", "[stat_cap]")
{
    mth::StatCapState caps;
    caps.set_counts(9, 5, 0);
    REQUIRE(caps.enforced_cap(0, 9) == 9); // count == vanilla: full unlock (displayed level 10)
    REQUIRE(caps.enforced_cap(1, 9) == 5); // count < vanilla: passes through unchanged
}

TEST_CASE("console-injected cap-up counts like a socket item and leaves the cursor", "[stat_cap]")
{
    mth::ApState s;
    s.apply(mth::ApItemReceived{{mth::kProgStatCapBase + 0, 0, 1, 0}}); // server attack cap-up, index 0
    s.inject_received_item(mth::kProgStatCapBase + 0);                  // console attack cap-up (no socket index)

    mth::StatCapState caps;
    caps.recompute(s);
    REQUIRE(caps.granted(0) == 2);

    // The injection must not advance last_item_index_, so a later server item (index 1) still applies.
    REQUIRE(s.last_item_index() == 0);
    s.apply(mth::ApItemReceived{{mth::kProgStatCapBase + 1, 1, 1, 0}});
    caps.recompute(s);
    REQUIRE(caps.granted(0) == 2);
    REQUIRE(caps.granted(1) == 1);
}

TEST_CASE("non-cap item ids are ignored", "[stat_cap]")
{
    mth::StatCapState caps;
    mth::ApState s3;
    make_state(s3, {mth::ap_item_id(5), mth::ap_item_id(42)});
    caps.recompute(s3);
    REQUIRE(caps.granted(0) == 0);
    REQUIRE(caps.granted(1) == 0);
    REQUIRE(caps.granted(2) == 0);
}

TEST_CASE("out-of-range stat yields vanilla cap", "[stat_cap]")
{
    mth::StatCapState caps;
    REQUIRE(caps.enforced_cap(3, 9) == 9);
    REQUIRE(caps.enforced_cap(-1, 9) == 9);
}

TEST_CASE("clamp_max_stat_level passes in-range values through", "[stat_cap]")
{
    REQUIRE(mth::clamp_max_stat_level(10) == 10);
    REQUIRE(mth::clamp_max_stat_level(30) == 30);
    REQUIRE(mth::clamp_max_stat_level(99) == 99);
}

TEST_CASE("clamp_max_stat_level clamps out-of-range to [10,99]", "[stat_cap]")
{
    REQUIRE(mth::clamp_max_stat_level(9) == 10);
    REQUIRE(mth::clamp_max_stat_level(0) == 10);
    REQUIRE(mth::clamp_max_stat_level(-5) == 10);
    REQUIRE(mth::clamp_max_stat_level(100) == 99);
    REQUIRE(mth::clamp_max_stat_level(1000) == 99);
}

TEST_CASE("stat_cap_ceiling: real stats use the slot_data max, others pass vanilla", "[stat_cap]")
{
    // attack/defense/sidearm (0..2) -> slot_data max_stat_level, replacing the native vanilla cap
    REQUIRE(mth::stat_cap_ceiling(0, 30, 14) == 30);
    REQUIRE(mth::stat_cap_ceiling(1, 30, 14) == 30);
    REQUIRE(mth::stat_cap_ceiling(2, 30, 14) == 30);
    // bone bank (3) and out-of-range -> native vanilla cap, untouched
    REQUIRE(mth::stat_cap_ceiling(3, 30, 14) == 14);
    REQUIRE(mth::stat_cap_ceiling(-1, 30, 14) == 14);
}
