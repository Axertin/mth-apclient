#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/ap/ap_events.hpp"
#include "mth/core/ap/ap_ids.hpp"
#include "mth/core/ap/ap_state.hpp"
#include "mth/core/wallet_cap_state.hpp"

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

TEST_CASE("wallet_cap_for: base cap with no wallet items", "[wallet_cap]")
{
    REQUIRE(mth::wallet_cap_for(0) == 750);
}

TEST_CASE("wallet_cap_for: each item adds 500", "[wallet_cap]")
{
    REQUIRE(mth::wallet_cap_for(1) == 1250);
    REQUIRE(mth::wallet_cap_for(2) == 1750);
    REQUIRE(mth::wallet_cap_for(7) == 4250);
}

TEST_CASE("wallet_cap_for: uncapped at and above 8 items", "[wallet_cap]")
{
    REQUIRE_FALSE(mth::wallet_cap_for(8).has_value());
    REQUIRE_FALSE(mth::wallet_cap_for(9).has_value());
    REQUIRE_FALSE(mth::wallet_cap_for(100).has_value());
}

TEST_CASE("wallet_cap_for: negative count clamps to base", "[wallet_cap]")
{
    REQUIRE(mth::wallet_cap_for(-1) == 750);
}

TEST_CASE("WalletCapState counts received wallet items", "[wallet_cap]")
{
    mth::ApState s;
    make_state(s, {mth::kProgWalletId, mth::kProgWalletId, mth::kProgWalletId});
    mth::WalletCapState w;
    w.recompute(s);
    REQUIRE(w.received() == 3);
    REQUIRE(w.enforced_cap() == 2250); // 750 + 500*3
}

TEST_CASE("WalletCapState ignores non-wallet items", "[wallet_cap]")
{
    mth::ApState s;
    make_state(s, {mth::ap_item_id(5), mth::kProgFishingRodId, mth::kMapItem});
    mth::WalletCapState w;
    w.recompute(s);
    REQUIRE(w.received() == 0);
    REQUIRE(w.enforced_cap() == 750);
}

TEST_CASE("WalletCapState uncapped once 8 items received", "[wallet_cap]")
{
    mth::ApState s;
    make_state(s, std::vector<std::int64_t>(8, mth::kProgWalletId));
    mth::WalletCapState w;
    w.recompute(s);
    REQUIRE(w.received() == 8);
    REQUIRE_FALSE(w.enforced_cap().has_value());
}

TEST_CASE("WalletCapState set_count seam", "[wallet_cap]")
{
    mth::WalletCapState w;
    w.set_count(1);
    REQUIRE(w.enforced_cap() == 1250);
}
