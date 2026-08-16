#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "mth/core/broadcast.hpp"

using Catch::Approx;
using mth::banner_color;
using mth::BannerQueue;
using mth::broadcast_relevant;
using mth::deathlink_banner_text;

TEST_CASE("broadcast_relevant: matches on our slot or receiving", "[mth][broadcast]")
{
    // our team 0, our slot 3
    REQUIRE(broadcast_relevant("ItemSend", 0, 3, 0, 3, std::nullopt, std::nullopt));            // slot is us
    REQUIRE(broadcast_relevant("ItemSend", 0, 3, 0, std::nullopt, 3, std::nullopt));            // receiving is us
    REQUIRE(broadcast_relevant("ItemSend", 0, 3, std::nullopt, std::nullopt, 3, std::nullopt)); // team absent -> treated as ours
    REQUIRE(broadcast_relevant("ItemSend", 0, 3, 0, 9, 3, std::nullopt));                       // receiving is us, slot someone else
}

TEST_CASE("broadcast_relevant: matches when we are the item finder (checks we send)", "[mth][broadcast]")
{
    // We (slot 3) found an item destined for slot 9: no top-level slot, receiving is someone else,
    // but the item's finder (item.player) is us. This is a check we sent -> relevant.
    REQUIRE(broadcast_relevant("ItemSend", 0, 3, std::nullopt, std::nullopt, 9, 3));
    REQUIRE(broadcast_relevant("ItemSend", 0, 3, 0, std::nullopt, 9, 3)); // team present + ours, finder us
    // Team still filters even when the finder is us.
    REQUIRE_FALSE(broadcast_relevant("ItemSend", 0, 3, 1, std::nullopt, 9, 3));
}

TEST_CASE("broadcast_relevant: filters out irrelevant", "[mth][broadcast]")
{
    REQUIRE_FALSE(broadcast_relevant("ItemSend", 0, 3, 0, 5, 7, 8));                                             // slot/receiving/finder all someone else
    REQUIRE_FALSE(broadcast_relevant("ItemSend", 0, 3, 1, 3, std::nullopt, 3));                                  // team mismatch (finder us but wrong team)
    REQUIRE_FALSE(broadcast_relevant("Hint", 0, 3, std::nullopt, std::nullopt, 9, std::nullopt));                // someone else's hint
    REQUIRE_FALSE(broadcast_relevant("Join", 0, 3, 0, 9, std::nullopt, std::nullopt));                           // someone else joined
    REQUIRE_FALSE(broadcast_relevant("ItemSend", 0, 3, std::nullopt, std::nullopt, std::nullopt, std::nullopt)); // no slot info at all
}

TEST_CASE("broadcast_relevant: announcements pass the slot gate", "[mth][broadcast]")
{
    // A /say from the server operator: no team, no slot, no item, so the slot gate has nothing to match.
    REQUIRE(broadcast_relevant("ServerChat", 0, 3, std::nullopt, std::nullopt, std::nullopt, std::nullopt));
    // Player chat carries the sender's slot; someone else's message is still shown.
    REQUIRE(broadcast_relevant("Chat", 0, 3, 0, 9, std::nullopt, std::nullopt));
    REQUIRE(broadcast_relevant("Countdown", 0, 3, std::nullopt, std::nullopt, std::nullopt, std::nullopt));
    REQUIRE(broadcast_relevant("CommandResult", 0, 3, std::nullopt, std::nullopt, std::nullopt, std::nullopt));
    // Team still filters when the message carries one.
    REQUIRE_FALSE(broadcast_relevant("Chat", 0, 3, 1, 9, std::nullopt, std::nullopt));
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
    REQUIRE(q.update(0.0).empty());
    REQUIRE(q.update(99.0).empty());
}

TEST_CASE("BannerQueue: shows a message and fades over hold+fade", "[mth][broadcast]")
{
    constexpr double hold = BannerQueue::kHoldSeconds;
    constexpr double fade = BannerQueue::kFadeSeconds;
    constexpr double t0 = 100.0;

    BannerQueue q;
    q.push({{"hi", 0xFFFFFFFFu}});

    const auto f0 = q.update(t0);
    REQUIRE(f0.size() == 1);
    REQUIRE(f0[0].segments.size() == 1);
    REQUIRE(f0[0].segments[0].text == "hi");
    REQUIRE(f0[0].alpha == Approx(1.0f));                               // within hold
    REQUIRE(q.update(t0 + hold * 0.5)[0].alpha == Approx(1.0f));        // still hold
    REQUIRE(q.update(t0 + hold + fade * 0.5)[0].alpha == Approx(0.5f)); // mid fade
    REQUIRE(q.update(t0 + hold + fade).empty());                        // fully faded -> gone
}

TEST_CASE("BannerQueue: stacks up to kMaxVisible, one promotion per interval", "[mth][broadcast]")
{
    constexpr double step = BannerQueue::kPromoteIntervalSeconds;

    BannerQueue q;
    for (int i = 0; i < BannerQueue::kMaxVisible + 2; ++i)
        q.push({{"msg", 0xFFFFFFFFu}});

    REQUIRE(q.update(0.0).size() == 1);        // a same-frame batch does not appear all at once
    REQUIRE(q.update(step * 0.5).size() == 1); // still inside the interval

    std::vector<mth::BannerFrame> frames;
    for (int i = 1; i < BannerQueue::kMaxVisible; ++i)
    {
        frames = q.update(step * i);
        REQUIRE(static_cast<int>(frames.size()) == i + 1);
    }
    for (const auto &f : frames)
        REQUIRE(f.alpha == Approx(1.0f)); // each shown independently, all within hold

    // The cap holds with extras pending even once the interval has elapsed.
    REQUIRE(static_cast<int>(q.update(step * BannerQueue::kMaxVisible).size()) == BannerQueue::kMaxVisible);
}

TEST_CASE("BannerQueue: a queued message waits until a visible slot frees, in order", "[mth][broadcast]")
{
    constexpr double step = BannerQueue::kPromoteIntervalSeconds;
    constexpr double life = BannerQueue::kHoldSeconds + BannerQueue::kFadeSeconds;

    BannerQueue q;
    for (int i = 0; i < BannerQueue::kMaxVisible; ++i)
        q.push({{"filler", 0xFFFFFFFFu}});
    q.push({{"overflow", 0xFFFFFFFFu}}); // one past the cap -> must wait

    // Fill every slot, one per interval; the first filler starts at 0.0 and the last at step*(kMaxVisible-1).
    for (int i = 0; i < BannerQueue::kMaxVisible; ++i)
        q.update(step * i);
    REQUIRE(static_cast<int>(q.update(step * BannerQueue::kMaxVisible).size()) == BannerQueue::kMaxVisible); // "overflow" still pending

    // The first filler fades out at `life`, freeing a slot for the overflow message.
    const auto after = q.update(life);
    REQUIRE(static_cast<int>(after.size()) == BannerQueue::kMaxVisible);
    REQUIRE(after.back().segments[0].text == "overflow"); // promoted last -> drawn at the bottom of the stack

    REQUIRE(q.update(life * 2.0).empty()); // overflow expired too
}

TEST_CASE("deathlink_banner_text: a cause is already a full sentence naming the sender", "[mth][broadcast][deathlink]")
{
    REQUIRE(deathlink_banner_text("Amaterasu", "Amaterasu was crushed by a spike trap") == "Amaterasu was crushed by a spike trap");
}

TEST_CASE("deathlink_banner_text: falls back to the sender when the cause is empty", "[mth][broadcast][deathlink]")
{
    REQUIRE(deathlink_banner_text("Amaterasu", "") == "Killed by Amaterasu");
}

TEST_CASE("deathlink_banner_text: a cause without a sender still stands alone", "[mth][broadcast][deathlink]")
{
    REQUIRE(deathlink_banner_text("", "somebody blew up") == "somebody blew up");
}

TEST_CASE("deathlink_banner_text: neither field leaves a generic attribution", "[mth][broadcast][deathlink]")
{
    REQUIRE(deathlink_banner_text("", "") == "Killed by a deathlink");
}
