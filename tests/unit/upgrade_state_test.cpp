#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/ap_events.hpp"
#include "mth/core/ap_ids.hpp"
#include "mth/core/ap_state.hpp"
#include "mth/core/upgrade_state.hpp"

namespace
{
// ApState is non-copyable/non-movable; populate in-place.
void make_state(mth::ApState &s, const std::vector<std::int64_t> &item_ids)
{
    int idx = 0;
    for (auto id : item_ids)
        s.apply(mth::ApItemReceived{{id, idx++, 0, 0}});
}
constexpr std::int64_t kMagic = mth::kUpgradeItemBase + 0;
constexpr std::int64_t kHealth = mth::kUpgradeItemBase + 1;
constexpr std::int64_t kTrinket = mth::kUpgradeItemBase + 4;
} // namespace

TEST_CASE("upgrade: default counts are zero and clean", "[upgrade]")
{
    mth::UpgradeState up;
    REQUIRE_FALSE(up.dirty());
    for (int i = 0; i < mth::kUpgradeCount; ++i)
        REQUIRE(up.counts()[i] == 0);
}

TEST_CASE("upgrade: counts receipts per type and flags dirty", "[upgrade]")
{
    mth::UpgradeState up;
    mth::ApState s1;
    make_state(s1, {kHealth, kHealth, kMagic});
    up.recompute(s1);
    REQUIRE(up.dirty());
    REQUIRE(up.counts()[0] == 1); // Magic
    REQUIRE(up.counts()[1] == 2); // Health
    REQUIRE(up.counts()[4] == 0); // Trinket
}

TEST_CASE("upgrade: clamps to the per-type cap", "[upgrade]")
{
    mth::UpgradeState up;
    std::vector<std::int64_t> many(50, kTrinket); // Trinket cap is 6
    mth::ApState s2;
    make_state(s2, many);
    up.recompute(s2);
    REQUIRE(up.counts()[4] == mth::kUpgradeCaps[4]);
    REQUIRE(up.counts()[4] == 6);
}

TEST_CASE("upgrade: mark_applied clears dirty until counts change", "[upgrade]")
{
    mth::UpgradeState up;
    mth::ApState s;
    make_state(s, {kHealth});
    up.recompute(s);
    REQUIRE(up.dirty());
    up.mark_applied();
    up.recompute(s); // same state: nothing new
    REQUIRE_FALSE(up.dirty());

    s.apply(mth::ApItemReceived{{kHealth, 99, 0, 0}}); // another Health
    up.recompute(s);
    REQUIRE(up.dirty());
    REQUIRE(up.counts()[1] == 2);
}

TEST_CASE("upgrade: force_dirty re-arms a re-apply that survives recompute", "[upgrade]")
{
    // A SaveSlot::Clear (new-file, but also profile-menu / save-load while AP-connected) zeros the upgrade
    // bitfields the kit suppressor targets. Counts are unchanged, so recompute alone would stay clean and the
    // fields would never be refilled (the "starting items missing on load" bug). force_dirty re-arms the apply
    // and recompute must not clear it before it lands.
    mth::UpgradeState up;
    mth::ApState s;
    make_state(s, {kHealth});
    up.recompute(s);
    up.mark_applied();
    up.recompute(s);
    REQUIRE_FALSE(up.dirty()); // steady state: clean

    up.force_dirty();
    REQUIRE(up.dirty());
    up.recompute(s); // unchanged counts must NOT clear the forced re-apply
    REQUIRE(up.dirty());
    up.mark_applied();
    up.recompute(s);
    REQUIRE_FALSE(up.dirty()); // landed: clean again
}

TEST_CASE("upgrade: non-upgrade items are ignored", "[upgrade]")
{
    mth::UpgradeState up;
    mth::ApState s3;
    make_state(s3, {mth::ap_item_id(5), mth::kProgWeaponBase, mth::kProgStatCapBase});
    up.recompute(s3);
    REQUIRE_FALSE(up.dirty());
    for (int i = 0; i < mth::kUpgradeCount; ++i)
        REQUIRE(up.counts()[i] == 0);
}
