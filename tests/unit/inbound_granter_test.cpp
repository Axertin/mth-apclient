#include <filesystem>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/ap_save_state.hpp"
#include "mth/core/ap_state.hpp"
#include "mth/core/inbound_granter.hpp"
#include "mth/core/item_granter.hpp"
#include "mth/core/rando_bridge.hpp"

namespace
{
struct FakeGranter : mth::IItemGranter
{
    std::vector<int> granted;
    bool ok = true;
    bool grant(int item_type) override
    {
        if (!ok)
            return false;
        granted.push_back(item_type);
        return true;
    }
};

mth::ApItemReceived recv(std::int64_t item_id, int index)
{
    mth::ApItemReceived e;
    e.item.item_id = item_id;
    e.item.index = index;
    return e;
}
} // namespace

TEST_CASE("InboundGranter grants new items once and dedups by index", "[inbound]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_inbound_state.txt";
    std::filesystem::remove(path);

    mth::ApState state;
    mth::ApSaveState save(path);
    FakeGranter granter;
    mth::InboundGranter inbound(granter, state, save);

    state.apply(recv(mth::ap_item_id(5), 0));
    state.apply(recv(mth::ap_item_id(9), 1));
    inbound.tick();
    REQUIRE(granter.granted == std::vector<int>{5, 9});

    inbound.tick();
    REQUIRE(granter.granted.size() == 2);

    state.apply(recv(mth::ap_item_id(2), 2));
    inbound.tick();
    REQUIRE(granter.granted == std::vector<int>{5, 9, 2});
    std::filesystem::remove(path);
}

TEST_CASE("InboundGranter does not mark on failure and retries", "[inbound]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_inbound_fail.txt";
    std::filesystem::remove(path);

    mth::ApState state;
    mth::ApSaveState save(path);
    FakeGranter granter;
    granter.ok = false;
    mth::InboundGranter inbound(granter, state, save);

    state.apply(recv(mth::ap_item_id(5), 0));
    inbound.tick();
    REQUIRE(granter.granted.empty());
    REQUIRE_FALSE(save.is_granted(0));

    granter.ok = true;
    inbound.tick();
    REQUIRE(granter.granted == std::vector<int>{5});
    REQUIRE(save.is_granted(0));
    std::filesystem::remove(path);
}

TEST_CASE("InboundGranter skips stat-cap items", "[inbound]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_inbound_caps.txt";
    std::filesystem::remove(path);

    mth::ApState state;
    mth::ApSaveState save(path);
    FakeGranter granter;
    mth::InboundGranter inbound(granter, state, save);

    state.apply(recv(mth::kProgStatCapBase + 0, 0)); // attack cap-up: must be skipped
    state.apply(recv(mth::ap_item_id(9), 1));        // a real item: must be granted
    inbound.tick();
    REQUIRE(granter.granted == std::vector<int>{9});

    std::filesystem::remove(path);
}

TEST_CASE("InboundGranter skips categories it cannot grant", "[inbound]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_inbound_reserved.txt";
    std::filesystem::remove(path);

    mth::ApState state;
    mth::ApSaveState save(path);
    FakeGranter granter;
    mth::InboundGranter inbound(granter, state, save);

    state.apply(recv(mth::kProgStatCapBase + 0, 0));   // stat-cap: StatCapState's job -> skipped
    state.apply(recv(mth::kKearBlockItemBase + 1, 1)); // lock removal -> skipped
    state.apply(recv(mth::kTrapItemBase + 7, 2));      // reserved, no handler -> skipped
    state.apply(recv(mth::ap_item_id(9), 3));          // vanilla item -> granted
    inbound.tick();
    REQUIRE(granter.granted == std::vector<int>{9});

    std::filesystem::remove(path);
}
