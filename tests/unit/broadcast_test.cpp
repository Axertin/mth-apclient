#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "mth/core/broadcast.hpp"

using Catch::Approx;
using mth::banner_color;
using mth::BannerQueue;
using mth::broadcast_relevant;

TEST_CASE("broadcast_relevant: matches on our slot or receiving", "[mth][broadcast]")
{
    // our team 0, our slot 3
    REQUIRE(broadcast_relevant(0, 3, 0, 3, std::nullopt, std::nullopt));            // slot is us
    REQUIRE(broadcast_relevant(0, 3, 0, std::nullopt, 3, std::nullopt));            // receiving is us
    REQUIRE(broadcast_relevant(0, 3, std::nullopt, std::nullopt, 3, std::nullopt)); // team absent -> treated as ours
    REQUIRE(broadcast_relevant(0, 3, 0, 9, 3, std::nullopt));                       // receiving is us, slot someone else
}

TEST_CASE("broadcast_relevant: matches when we are the item finder (checks we send)", "[mth][broadcast]")
{
    // We (slot 3) found an item destined for slot 9: no top-level slot, receiving is someone else,
    // but the item's finder (item.player) is us. This is a check we sent -> relevant.
    REQUIRE(broadcast_relevant(0, 3, std::nullopt, std::nullopt, 9, 3));
    REQUIRE(broadcast_relevant(0, 3, 0, std::nullopt, 9, 3)); // team present + ours, finder us
    // Team still filters even when the finder is us.
    REQUIRE_FALSE(broadcast_relevant(0, 3, 1, std::nullopt, 9, 3));
}

TEST_CASE("broadcast_relevant: filters out irrelevant", "[mth][broadcast]")
{
    REQUIRE_FALSE(broadcast_relevant(0, 3, 0, 5, 7, 8));                                             // slot/receiving/finder all someone else
    REQUIRE_FALSE(broadcast_relevant(0, 3, 1, 3, std::nullopt, 3));                                  // team mismatch (finder us but wrong team)
    REQUIRE_FALSE(broadcast_relevant(0, 3, std::nullopt, std::nullopt, std::nullopt, std::nullopt)); // no slot info at all
}

TEST_CASE("banner_color: type/flags select distinct AP colors", "[mth][broadcast]")
{
    const auto progression = banner_color("item_id", "", 1u, 0u, false); // FLAG_ADVANCEMENT
    const auto useful = banner_color("item_id", "", 2u, 0u, false);      // FLAG_NEVER_EXCLUDE
    const auto trap = banner_color("item_id", "", 4u, 0u, false);        // FLAG_TRAP
    const auto filler = banner_color("item_id", "", 0u, 0u, false);
    REQUIRE(progression != filler);
    REQUIRE(useful != filler);
    REQUIRE(trap != filler);
    REQUIRE(progression != useful);

    // player_id self vs other differ
    REQUIRE(banner_color("player_id", "", 0u, 0u, true) != banner_color("player_id", "", 0u, 0u, false));

    // an explicit node color overrides the type-derived color
    REQUIRE(banner_color("item_id", "red", 1u, 0u, false) == banner_color("text", "red", 0u, 0u, false));

    // every color is fully opaque (alpha byte set); the fade scales it later
    REQUIRE((filler >> 24) == 0xFFu);
}

TEST_CASE("BannerQueue: idle returns nothing", "[mth][broadcast]")
{
    BannerQueue q;
    REQUIRE_FALSE(q.update(0.0).has_value());
    REQUIRE_FALSE(q.update(99.0).has_value());
}

TEST_CASE("BannerQueue: shows a message and fades over hold+fade", "[mth][broadcast]")
{
    constexpr double hold = BannerQueue::kHoldSeconds;
    constexpr double fade = BannerQueue::kFadeSeconds;
    constexpr double t0 = 100.0;

    BannerQueue q;
    q.push({{"hi", 0xFFFFFFFFu}});

    const auto f0 = q.update(t0);
    REQUIRE(f0.has_value());
    REQUIRE(f0->segments.size() == 1);
    REQUIRE(f0->segments[0].text == "hi");
    REQUIRE(f0->alpha == Approx(1.0f));                               // within hold
    REQUIRE(q.update(t0 + hold * 0.5)->alpha == Approx(1.0f));        // still hold
    REQUIRE(q.update(t0 + hold + fade * 0.5)->alpha == Approx(0.5f)); // mid fade
    REQUIRE_FALSE(q.update(t0 + hold + fade).has_value());            // fully faded -> gone
}

TEST_CASE("BannerQueue: drains queued messages in order", "[mth][broadcast]")
{
    constexpr double life = BannerQueue::kHoldSeconds + BannerQueue::kFadeSeconds;

    BannerQueue q;
    q.push({{"first", 0xFFFFFFFFu}});
    q.push({{"second", 0xFFFFFFFFu}});

    REQUIRE(q.update(0.0)->segments[0].text == "first");
    REQUIRE(q.update(life * 0.5)->segments[0].text == "first");  // still first
    REQUIRE(q.update(life + 0.1)->segments[0].text == "second"); // first expired, second begins
    REQUIRE(q.update(life + 0.2)->segments[0].text == "second");
    REQUIRE_FALSE(q.update(2.0 * life + 0.2).has_value()); // second expired too
}
