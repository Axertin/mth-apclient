#include <string>
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

TEST_CASE("boneup_fake_capped_stat gates the fake to the interactive state, the selected stat, and the cap", "[boneup]")
{
    // Not interactive: never maxed, even selected at/over cap.
    REQUIRE_FALSE(mth::boneup_fake_capped_stat(false, true, 5, 5));
    REQUIRE_FALSE(mth::boneup_fake_capped_stat(false, true, 9, 3));

    // Interactive, selected, at/over cap: maxed.
    REQUIRE(mth::boneup_fake_capped_stat(true, true, 5, 5));
    REQUIRE(mth::boneup_fake_capped_stat(true, true, 6, 5));

    // Not the selected stat: leave real (else the sentinel leaks into the commit's defense re-apply).
    REQUIRE_FALSE(mth::boneup_fake_capped_stat(true, false, 5, 5));
    REQUIRE_FALSE(mth::boneup_fake_capped_stat(true, false, 9, 3));

    // Selected but below cap: leave real (genuinely buyable).
    REQUIRE_FALSE(mth::boneup_fake_capped_stat(true, true, 4, 5));
}

TEST_CASE("boneup_display_cap is the level shown once the buy gate closes", "[boneup]")
{
    // The gate is `raw_level < cap` and the menu renders raw+1, so a cap of 9 makes raw 8 the last
    // buyable step and 10 the level it lands on, which is also what vanilla tops out at.
    REQUIRE_FALSE(mth::boneup_fake_capped_stat(true, true, 8, 9));
    REQUIRE(mth::boneup_fake_capped_stat(true, true, 9, 9));
    REQUIRE(mth::boneup_display_cap(9) == 10);
}

namespace
{
// The annotation is an in-place edit of the title line, so what holds for any description is that
// everything from the first newline on survives untouched and the title only gained a suffix.
void require_first_line_grew(const std::string &in, const std::string &out)
{
    const std::size_t in_eol = in.find('\n');
    const std::size_t out_eol = out.find('\n');
    REQUIRE(out.substr(out_eol == std::string::npos ? out.size() : out_eol) == in.substr(in_eol == std::string::npos ? in.size() : in_eol));

    const std::string in_head = in.substr(0, in_eol);
    const std::string out_head = out.substr(0, out_eol);
    REQUIRE(out_head.size() > in_head.size());
    REQUIRE(out_head.starts_with(in_head));
}
} // namespace

TEST_CASE("boneup_with_cap_suffix annotates the first line and leaves the rest byte-identical", "[boneup]")
{
    const std::string desc = "Attack Level 1\nNext level at 175 Bones\nThe power of your main attack.";
    require_first_line_grew(desc, mth::boneup_with_cap_suffix(desc, 3));
}

TEST_CASE("boneup_with_cap_suffix is idempotent across frames", "[boneup]")
{
    const std::string once = mth::boneup_with_cap_suffix("Attack Level 1\nNext level at 175 Bones", 3);
    REQUIRE(mth::boneup_with_cap_suffix(once, 3) == once);
}

TEST_CASE("boneup_with_cap_suffix replaces a stale cap when the cap changes mid-menu", "[boneup]")
{
    // A description the walk already annotated has to be indistinguishable from the vanilla one as an
    // input, or a cap raised while the menu is open stacks a second suffix onto the first.
    const std::string base = "Attack Level 1\nNext level at 175 Bones";
    REQUIRE(mth::boneup_with_cap_suffix(mth::boneup_with_cap_suffix(base, 2), 3) == mth::boneup_with_cap_suffix(base, 3));
    REQUIRE(mth::boneup_with_cap_suffix(base, 2) != mth::boneup_with_cap_suffix(base, 3));
}

TEST_CASE("boneup_with_cap_suffix handles a single-line description", "[boneup]")
{
    const std::string desc = "Attack Level 1";
    require_first_line_grew(desc, mth::boneup_with_cap_suffix(desc, 3));
}

TEST_CASE("boneup_with_cap_suffix leaves empty text alone", "[boneup]")
{
    REQUIRE(mth::boneup_with_cap_suffix("", 3).empty());
}

TEST_CASE("boneup_with_cap_suffix only strips a numeric parenthesised suffix", "[boneup]")
{
    // Localized text may legitimately end in parentheses; only a bare number is ours to replace, so
    // anything else survives whole into the annotated line.
    REQUIRE(mth::boneup_with_cap_suffix("Attack (special) Level 1", 3).starts_with("Attack (special) Level 1"));
    REQUIRE(mth::boneup_with_cap_suffix("Attack Level 1 (max)", 3).starts_with("Attack Level 1 (max)"));
    // Ours does get stripped, so the input is not a prefix of the result.
    REQUIRE_FALSE(mth::boneup_with_cap_suffix("Attack Level 1 (2)", 3).starts_with("Attack Level 1 (2)"));
}

TEST_CASE("boneup_with_cap_suffix leaves text alone when the first line is empty", "[boneup]")
{
    // No title to annotate; appending would produce a bare " (3)" as the first line.
    REQUIRE(mth::boneup_with_cap_suffix("\nNext level at 175 Bones", 3) == "\nNext level at 175 Bones");
}

namespace
{
// The panel's level widget wraps at a fixed width and renders the overflow ABOVE the box, so the
// constraint on the annotated label is its rendered width, not its wording. Nothing outside the game
// can measure that width, so the tests below use a character count as the proxy: "LVL 9 (14)" was
// reported rendering on one line and "LVL 14 (14)" was reported wrapping, which makes ten the longest
// label with in-game evidence behind it.
constexpr std::size_t kPanelFitChars = 10;
} // namespace

TEST_CASE("status_panel_with_cap_suffix keeps every reachable level and cap within the measured budget", "[boneup]")
{
    // Levels and caps both top out at the game's absolute ceiling of 99, displayed as level+1.
    for (int level = 1; level <= 100; ++level)
    {
        const std::string label = "LVL " + std::to_string(level);
        for (int cap = 1; cap <= 100; ++cap)
        {
            const std::string annotated = mth::status_panel_with_cap_suffix(label, cap);
            INFO("level " << level << " cap " << cap << " -> " << annotated);
            REQUIRE(annotated.size() <= kPanelFitChars);
            REQUIRE(annotated.size() > label.size());
        }
    }
}

TEST_CASE("status_panel_with_cap_suffix does not stack when re-applied or when the cap changes", "[boneup]")
{
    // The walk re-annotates on a cadence while the pause screen is open, so a label it already wrote
    // has to be indistinguishable from the vanilla one as an input.
    const std::string base = "LVL 14";
    const std::string annotated = mth::status_panel_with_cap_suffix(base, 3);
    REQUIRE(mth::status_panel_with_cap_suffix(annotated, 3) == annotated);
    REQUIRE(mth::status_panel_with_cap_suffix(annotated, 15) == mth::status_panel_with_cap_suffix(base, 15));
}

TEST_CASE("status_panel_with_cap_suffix shows a cap past the ceiling as the ceiling", "[boneup]")
{
    REQUIRE(mth::status_panel_with_cap_suffix("LVL 99", 100) == mth::status_panel_with_cap_suffix("LVL 99", 99));
}

TEST_CASE("status_panel_with_cap_suffix leaves empty text alone", "[boneup]")
{
    REQUIRE(mth::status_panel_with_cap_suffix("", 3).empty());
}
