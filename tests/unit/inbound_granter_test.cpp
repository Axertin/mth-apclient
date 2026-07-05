#include <filesystem>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/ap/ap_save_state.hpp"
#include "mth/core/ap/ap_state.hpp"
#include "mth/core/inbound_granter.hpp"
#include "mth/core/item_granter_interface.hpp"
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

TEST_CASE("InboundGranter translates progressive weapons to tiered itemTypes", "[inbound]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_inbound_weapons.txt";
    std::filesystem::remove(path);

    mth::ApState state;
    mth::ApSaveState save(path);
    FakeGranter granter;
    mth::InboundGranter inbound(granter, state, save);

    // Whip family (kProgWeaponBase) received 3x -> grants Whip/WhipLevel2/WhipLevel3 = itemTypes 2,3,4.
    state.apply(recv(mth::kProgWeaponBase, 0));
    state.apply(recv(mth::kProgWeaponBase, 1));
    state.apply(recv(mth::kProgWeaponBase, 2));
    // Casket family (kProgWeaponBase + 4) once -> itemType 14, interleaved by index order.
    state.apply(recv(mth::kProgWeaponBase + 4, 3));
    inbound.tick();
    REQUIRE(granter.granted == std::vector<int>{2, 3, 4, 14});

    // Idempotent: a re-tick (e.g. after reload) grants nothing new.
    granter.granted.clear();
    inbound.tick();
    REQUIRE(granter.granted.empty());

    std::filesystem::remove(path);
}

TEST_CASE("InboundGranter translates the progressive fishing rod to tiered upgrade itemTypes", "[inbound]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_inbound_fishing.txt";
    std::filesystem::remove(path);

    mth::ApState state;
    mth::ApSaveState save(path);
    FakeGranter granter;
    mth::InboundGranter inbound(granter, state, save);

    // Progressive Fishing Rod received 3x -> grants Upgrade_FishingRod/FishingUpgrade/FishingGold = 87,88,89.
    state.apply(recv(mth::kProgFishingRodId, 0));
    state.apply(recv(mth::kProgFishingRodId, 1));
    state.apply(recv(mth::kProgFishingRodId, 2));
    inbound.tick();
    REQUIRE(granter.granted == std::vector<int>{87, 88, 89});

    // Idempotent: a re-tick (e.g. after reload) grants nothing new.
    granter.granted.clear();
    inbound.tick();
    REQUIRE(granter.granted.empty());

    std::filesystem::remove(path);
}

TEST_CASE("InboundGranter consumes a progressive fishing rod beyond its top tier without granting", "[inbound]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_inbound_fishing_overflow.txt";
    std::filesystem::remove(path);

    mth::ApState state;
    mth::ApSaveState save(path);
    FakeGranter granter;
    mth::InboundGranter inbound(granter, state, save);

    state.apply(recv(mth::kProgFishingRodId, 0));
    state.apply(recv(mth::kProgFishingRodId, 1));
    state.apply(recv(mth::kProgFishingRodId, 2));
    state.apply(recv(mth::kProgFishingRodId, 3)); // 4th: beyond tier 3 -> consumed, not granted, no retry
    inbound.tick();
    REQUIRE(granter.granted == std::vector<int>{87, 88, 89});
    REQUIRE(save.is_granted(3));

    std::filesystem::remove(path);
}

TEST_CASE("InboundGranter retries the fishing rod at its correct tier after a failure", "[inbound]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_inbound_fishing_retry.txt";
    std::filesystem::remove(path);

    mth::ApState state;
    mth::ApSaveState save(path);
    FakeGranter granter;
    granter.ok = false; // player not ready yet
    mth::InboundGranter inbound(granter, state, save);

    state.apply(recv(mth::kProgFishingRodId, 0)); // tier 1
    state.apply(recv(mth::kProgFishingRodId, 1)); // tier 2
    inbound.tick();
    REQUIRE(granter.granted.empty());
    REQUIRE_FALSE(save.is_granted(0));

    granter.ok = true;
    inbound.tick();
    REQUIRE(granter.granted == std::vector<int>{87, 88}); // tiers recomputed correctly, both granted

    std::filesystem::remove(path);
}

TEST_CASE("InboundGranter retries a weapon at its correct tier after a failure", "[inbound]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_inbound_weapon_retry.txt";
    std::filesystem::remove(path);

    mth::ApState state;
    mth::ApSaveState save(path);
    FakeGranter granter;
    granter.ok = false; // player not ready yet
    mth::InboundGranter inbound(granter, state, save);

    state.apply(recv(mth::kProgWeaponBase, 0)); // Whip tier 1
    state.apply(recv(mth::kProgWeaponBase, 1)); // Whip tier 2
    inbound.tick();
    REQUIRE(granter.granted.empty());
    REQUIRE_FALSE(save.is_granted(0));

    granter.ok = true;
    inbound.tick();
    REQUIRE(granter.granted == std::vector<int>{2, 3}); // tiers recomputed correctly, both granted

    std::filesystem::remove(path);
}
