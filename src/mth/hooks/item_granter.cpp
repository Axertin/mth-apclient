#include "mth/hooks/item_granter.hpp"

#include <cmath>
#include <functional>
#include <mutex>
#include <vector>

#include "mth/core/game_layout.hpp"
#include "mth/core/game_symbols.hpp"
#include "mth/hooks/game_tables.hpp"
#include "mth/hooks/player_tracker.hpp"
#include "pal/pal_log.hpp"

namespace
{

struct YcVec3
{
    float x, y, z;
};

mth::PlayerTracker *g_tracker = nullptr;
std::function<bool(int)> g_is_ap_location;   // RandoBridge::is_ap_location, wired by App
std::function<void(int)> g_report_collected; // RandoBridge::on_location_collected, wired by App

// Items::OnPickupDone(int slot, int itemType, Player*, ycVec3 const&, int, int, unsigned int, bool)
void (*g_orig_on_pickup_done)(int, int, void *, void *, int, int, unsigned int, bool) = nullptr;
// Items::OnPickup(int slot, int itemType, Player*, ycVec3 const&, bool, int, int, unsigned int, bool)
void (*g_orig_on_pickup)(int, int, void *, void *, bool, int, int, unsigned int, bool) = nullptr;

// Inbound-grant queue: grant() enqueues, drain() replays inside the engine's update window.
std::mutex g_pending_mtx;
std::vector<int> g_pending;

void repl_on_pickup_done(int slot, int item_type, void *player, void *vec, int a5, int a6, unsigned int a7, bool a8)
{
    if (g_tracker != nullptr)
        g_tracker->note_player(player); // refresh the grant-target player for inbound replays

    // Skip vanilla grants for randomized locations. AP replays use slot==-1; world AP pickups carry the
    // dummy itemType; only a real item at an AP slot is the vanilla grant the server overrides.
    if (slot >= 0 && item_type != mth::layout::kApDummyItemType && g_is_ap_location && g_is_ap_location(slot))
    {
        pal::logf(pal::LogLevel::Info, "outbound: suppressed vanilla grant for AP location %d (itemType=%d)", slot, item_type);
        // Grants delivered straight through OnPickupDone (no Pickup entity, no ShopMenu) -- the train-ticket
        // machine -- never reach the pickup/shop detect hooks, so send the check here. Idempotent: the bridge
        // dedups, so the world-pickup (dummy itemType, excluded above) and Windows-shop paths can't double-send.
        if (g_report_collected)
            g_report_collected(slot);
        return;
    }

    if (g_orig_on_pickup_done)
        g_orig_on_pickup_done(slot, item_type, player, vec, a5, a6, a7, a8);
}

// Items::OnPickup runs before OnPickupDone and, for armor upgrades (Vitality Vest 0x4f, Damage armor 0x50),
// ORs the upgrade bit into SaveSlot+0xc68 *itself* -- so suppressing only OnPickupDone leaks the vanilla
// armor for an AP shop buy (issue #71). Suppress those armor types for AP locations here, at the real
// chokepoint. Scoped to the two armor itemTypes so every other pickup flows through unchanged (OnPickupDone
// still does the per-location suppression for them). Idempotent collect-report mirrors OnPickupDone.
void repl_on_pickup(int slot, int item_type, void *player, void *vec, bool a5, int a6, int a7, unsigned int a8, bool a9)
{
    if (g_tracker != nullptr)
        g_tracker->note_player(player);

    if (slot >= 0 && mth::tables::is_armor_upgrade_itemtype(item_type) && g_is_ap_location && g_is_ap_location(slot))
    {
        pal::logf(pal::LogLevel::Info, "outbound: suppressed vanilla armor upgrade for AP location %d (itemType=%d)", slot, item_type);
        if (g_report_collected)
            g_report_collected(slot);
        return;
    }

    if (g_orig_on_pickup)
        g_orig_on_pickup(slot, item_type, player, vec, a5, a6, a7, a8, a9);
}

} // namespace

namespace mth
{

ItemGranter::ItemGranter(PlayerTracker &tracker, std::function<bool(int)> is_ap_location, std::function<void(int)> report_collected)
{
    g_tracker = &tracker;
    g_is_ap_location = std::move(is_ap_location);
    g_report_collected = std::move(report_collected);
    pickup_done_ = ScopedHook(sym::on_pickup_done, reinterpret_cast<void *>(&repl_on_pickup_done), reinterpret_cast<void **>(&g_orig_on_pickup_done),
                              "Items::OnPickupDone");
    pickup_ = ScopedHook(sym::on_pickup, reinterpret_cast<void *>(&repl_on_pickup), reinterpret_cast<void **>(&g_orig_on_pickup), "Items::OnPickup");
}

ItemGranter::~ItemGranter()
{
    std::lock_guard<std::mutex> lk(g_pending_mtx);
    g_pending.clear();
    g_tracker = nullptr;
    g_is_ap_location = nullptr;
    g_report_collected = nullptr;
}

bool ItemGranter::grant(int item_type)
{
    // Require hook + Player* before accepting; return false to retry next tick.
    if (g_orig_on_pickup_done == nullptr || g_tracker == nullptr || g_tracker->player() == nullptr)
        return false;

    // itemType 0 = engine "None" sentinel; treat as handled so the cursor advances.
    if (item_type <= 0)
        return true;

    std::lock_guard<std::mutex> lk(g_pending_mtx);
    g_pending.push_back(item_type);
    return true;
}

void ItemGranter::drain()
{
    std::vector<int> batch;
    {
        std::lock_guard<std::mutex> lk(g_pending_mtx);
        if (g_pending.empty())
            return;
        batch.swap(g_pending);
    }

    // Position comes from the tracker's in-context cache; reading it here (pre-World::Update
    // spawn window) would walk an invalid camera graph and fault. Requeue until cached.
    float p[3];
    void *player = g_tracker != nullptr ? g_tracker->player() : nullptr;
    const bool ready = g_orig_on_pickup_done && player != nullptr && g_tracker->position(p);
    if (!ready || !std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]))
    {
        std::lock_guard<std::mutex> lk(g_pending_mtx);
        g_pending.insert(g_pending.begin(), batch.begin(), batch.end());
        return;
    }

    // locIdx=-1: grant by type only, no location state touched.
    for (int item_type : batch)
    {
        YcVec3 grant_pos{p[0], p[1], p[2]}; // fresh copy per item (ycVec3 const&)
        g_orig_on_pickup_done(-1, item_type, player, &grant_pos, 0, 0, 0, false);
        pal::logf(pal::LogLevel::Info, "inbound: granted item_type=%d (kind=%d)", item_type, tables::storage_kind(item_type));
    }
}

} // namespace mth
