#include <filesystem>
#include <memory>
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
    std::vector<int> granted;  // itemTypes accepted
    std::vector<int> in_queue; // receipts accepted but not yet applied (defer mode)
    bool ok = true;
    bool defer = false; // model the real granter's queue: accept now, apply on a later drain

    bool grant(int item_type, int receipt) override
    {
        if (!ok)
            return false;
        granted.push_back(item_type);
        if (defer)
            in_queue.push_back(receipt);
        else
            notify_applied(receipt);
        return true;
    }

    void discard_pending() override
    {
        in_queue.clear();
    }

    // the drain window: everything queued actually lands now
    void apply_queued()
    {
        std::vector<int> batch;
        batch.swap(in_queue);
        for (int receipt : batch)
            notify_applied(receipt);
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

// #130: in vanilla kear mode a received Universal Kear (id 63) must grant a real usable key. It does NOT
// go through the itemType-grant path (OnPickupDone with slot=-1 aliases every kear onto bit 63); instead
// each new receipt fires the injected key-credit effect once, marked durable per index like any grant.
TEST_CASE("InboundGranter credits vanilla kears instead of granting itemType 63", "[inbound]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_inbound_kear_vanilla.txt";
    std::filesystem::remove(path);

    mth::ApState state;
    state.apply(mth::ApConnected{{}, "{}", 1, {}, {}, false, mth::KearMode::Vanilla});
    mth::ApSaveState save(path);
    FakeGranter granter;
    int credits = 0;
    mth::InboundGranter inbound(granter, state, save,
                                [&credits]
                                {
                                    ++credits;
                                    return true;
                                });

    state.apply(recv(mth::ap_item_id(63), 0));
    state.apply(recv(mth::ap_item_id(63), 1));
    inbound.tick();
    REQUIRE(credits == 2);
    REQUIRE(granter.granted.empty()); // never routed through the itemType-63 grant
    REQUIRE(save.is_granted(0));
    REQUIRE(save.is_granted(1));

    inbound.tick(); // resend/next tick: already credited, no double
    REQUIRE(credits == 2);

    std::filesystem::remove(path);
}

TEST_CASE("InboundGranter retries a vanilla kear credit that is not ready", "[inbound]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_inbound_kear_retry.txt";
    std::filesystem::remove(path);

    mth::ApState state;
    state.apply(mth::ApConnected{{}, "{}", 1, {}, {}, false, mth::KearMode::Vanilla});
    mth::ApSaveState save(path);
    FakeGranter granter;
    bool ready = false;
    int credits = 0;
    mth::InboundGranter inbound(granter, state, save,
                                [&]
                                {
                                    if (!ready)
                                        return false;
                                    ++credits;
                                    return true;
                                });

    state.apply(recv(mth::ap_item_id(63), 0));
    inbound.tick();
    REQUIRE(credits == 0);
    REQUIRE_FALSE(save.is_granted(0)); // not marked while unavailable

    ready = true;
    inbound.tick();
    REQUIRE(credits == 1);
    REQUIRE(save.is_granted(0));

    std::filesystem::remove(path);
}

// In the AP-item kear modes the pool never carries id 63; the credit effect must stay dormant so those
// modes keep their existing behavior (this fix is vanilla-only).
TEST_CASE("InboundGranter does not credit kears outside vanilla mode", "[inbound]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_inbound_kear_apitems.txt";
    std::filesystem::remove(path);

    mth::ApState state;
    state.apply(mth::ApConnected{{}, "{}", 1, {}, {}, false, mth::KearMode::ApItems});
    mth::ApSaveState save(path);
    FakeGranter granter;
    int credits = 0;
    mth::InboundGranter inbound(granter, state, save,
                                [&credits]
                                {
                                    ++credits;
                                    return true;
                                });

    state.apply(recv(mth::ap_item_id(63), 0));
    inbound.tick();
    REQUIRE(credits == 0);
    REQUIRE(granter.granted == std::vector<int>{63}); // falls through to the normal vanilla-grant path

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

// #175: the real granter only QUEUES on grant(); the item lands later, inside the engine's update
// window. Persisting "granted" on acceptance loses the item for good if that window never comes,
// because is_granted() then suppresses it on every future launch.
TEST_CASE("InboundGranter does not persist a receipt the granter has only queued", "[inbound]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_inbound_queued.txt";
    std::filesystem::remove(path);

    mth::ApState state;
    mth::ApSaveState save(path);
    FakeGranter granter;
    granter.defer = true;
    mth::InboundGranter inbound(granter, state, save);

    state.apply(recv(mth::ap_item_id(5), 0));
    inbound.tick();
    REQUIRE(granter.granted == std::vector<int>{5}); // accepted
    REQUIRE_FALSE(save.is_granted(0));               // but not applied, so not durable yet

    granter.apply_queued();
    REQUIRE(save.is_granted(0));

    std::filesystem::remove(path);
}

// The in-flight receipt is not durable yet, so the next tick must not hand it to the granter again.
TEST_CASE("InboundGranter does not re-queue an in-flight receipt", "[inbound]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_inbound_inflight.txt";
    std::filesystem::remove(path);

    mth::ApState state;
    mth::ApSaveState save(path);
    FakeGranter granter;
    granter.defer = true;
    mth::InboundGranter inbound(granter, state, save);

    state.apply(recv(mth::ap_item_id(5), 0));
    inbound.tick();
    inbound.tick();
    inbound.tick();
    REQUIRE(granter.granted == std::vector<int>{5}); // queued once, not once per tick

    granter.apply_queued();
    inbound.tick();
    REQUIRE(granter.granted == std::vector<int>{5}); // and not again after it lands
    REQUIRE(save.is_granted(0));

    std::filesystem::remove(path);
}

// The loss window in #175: the player quits with the batch still queued. Nothing was persisted, so a
// fresh granter over the same save file must hand the item out again rather than skip it forever.
TEST_CASE("InboundGranter retries a receipt that was queued but never applied", "[inbound]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_inbound_lost_queue.txt";
    std::filesystem::remove(path);

    mth::ApState state;
    state.apply(recv(mth::ap_item_id(5), 0));

    {
        mth::ApSaveState save(path);
        FakeGranter granter;
        granter.defer = true;
        mth::InboundGranter inbound(granter, state, save);
        inbound.tick();
        REQUIRE(granter.granted == std::vector<int>{5});
        REQUIRE_FALSE(save.is_granted(0));
    } // process exit: the queue dies with it

    // relaunch: the server replays the same stream against the persisted state
    mth::ApSaveState reloaded(path);
    REQUIRE_FALSE(reloaded.is_granted(0));
    FakeGranter granter;
    mth::InboundGranter inbound(granter, state, reloaded);
    inbound.tick();
    REQUIRE(granter.granted == std::vector<int>{5}); // recovered, not lost
    REQUIRE(reloaded.is_granted(0));

    std::filesystem::remove(path);
}

// A session change drops the queue, so those receipts must be retried against the new save rather
// than acked into it.
TEST_CASE("InboundGranter re-queues receipts dropped by a session change", "[inbound]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_inbound_discard.txt";
    std::filesystem::remove(path);

    mth::ApState state;
    mth::ApSaveState save(path);
    FakeGranter granter;
    granter.defer = true;
    state.apply(recv(mth::ap_item_id(5), 0));

    {
        mth::InboundGranter inbound(granter, state, save);
        inbound.tick();
        REQUIRE(granter.granted == std::vector<int>{5});
    }
    granter.discard_pending(); // what GrantPipeline::release_inbound does
    granter.granted.clear();

    mth::InboundGranter rebuilt(granter, state, save); // the next session, against its own save
    rebuilt.tick();
    REQUIRE(granter.granted == std::vector<int>{5}); // offered again, never silently dropped
    REQUIRE_FALSE(save.is_granted(0));

    std::filesystem::remove(path);
}

// The granter outlives its InboundGranter and holds the applied sink. If a replacement is built
// before the incumbent is torn down, the incumbent's teardown must disarm only its own sink. Wiping
// the installed one leaves grants applying with nothing marked, so they all re-apply next launch.
TEST_CASE("InboundGranter teardown does not disarm a successor's applied sink", "[inbound]")
{
    const auto path = std::filesystem::temp_directory_path() / "mthap_inbound_sink_handover.txt";
    std::filesystem::remove(path);

    mth::ApState state;
    mth::ApSaveState save(path);
    FakeGranter granter;
    granter.defer = true;
    state.apply(recv(mth::ap_item_id(5), 0));

    auto first = std::make_unique<mth::InboundGranter>(granter, state, save);
    auto second = std::make_unique<mth::InboundGranter>(granter, state, save); // built before...
    first.reset();                                                             // ...the incumbent goes

    second->tick();
    REQUIRE(granter.granted == std::vector<int>{5});
    granter.apply_queued();
    REQUIRE(save.is_granted(0)); // false if the teardown wiped the live sink

    std::filesystem::remove(path);
}
