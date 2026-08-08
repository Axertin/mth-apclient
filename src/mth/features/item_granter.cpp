#include "mth/features/item_granter.hpp"

#include <cmath>
#include <functional>
#include <mutex>
#include <vector>

#include "mod/mod_api.hpp"
#include "mth/core/ap/ap_ids.hpp"
#include "mth/core/data/game_layout.hpp"
#include "mth/core/data/game_symbols.hpp"
#include "mth/core/data/game_tables.hpp"
#include "mth/features/player_tracker.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

namespace
{

struct YcVec3
{
    float x, y, z;
};

mth::PlayerTracker *g_tracker = nullptr;
std::function<bool(int)> g_is_ap_location;   // RandoBridge::is_ap_location, wired by App
std::function<void(int)> g_report_collected; // RandoBridge::on_location_collected, wired by App
bool g_train_pass_machine_blocked = false;   // train_rando: the donation machine grants nothing (#162)

// Items::OnPickupDone(int slot, int itemType, Player*, ycVec3 const&, int, int, unsigned int, bool).
// Resolved but deliberately NOT detoured: suppression runs through the ItemsOnPickupDone named hook,
// while an inbound AP grant has to CALL this function, and a named hook hands back no callable
// original. Calling it re-enters our own hook with slot -1, which the suppression check passes over.
void (*g_call_on_pickup_done)(int, int, void *, void *, int, int, unsigned int, bool) = nullptr;

// Inbound-grant queue: grant() enqueues, drain() replays inside the engine's update window.
// The receipt rides along so drain() can ack the caller only for what actually landed (#175).
struct PendingGrant
{
    int item_type;
    int receipt;
};
std::mutex g_pending_mtx;
std::vector<PendingGrant> g_pending;

// Start of Items::OnPickupDone. Returns true to stop the original running.
bool on_items_pickup_done(int slot, int item_type, void *player)
{
    if (g_tracker != nullptr)
        g_tracker->note_player(player); // refresh the grant-target player for inbound replays

    // These re-seed the vial bitfield behind the AP count (#171); App re-asserts it, so this is a trace
    // of what fired, not a suppression point.
    if (item_type == mth::layout::kItemHealingVialFirst || item_type == mth::layout::kItemVialUpgrade)
        pal::logf(pal::LogLevel::Debug, "vials: OnPickupDone itemType=%#x slot=%d", item_type, slot);

    // Skip vanilla grants for randomized locations. AP replays use slot==-1; world AP pickups carry the
    // dummy itemType; only a real item at an AP slot is the vanilla grant the server overrides.
    if (slot >= 0 && item_type != mth::layout::kApDummyItemType && g_is_ap_location && g_is_ap_location(slot))
    {
        pal::logf(pal::LogLevel::Info, "outbound: suppressed vanilla grant for AP location %d (itemType=%d)", slot, item_type);
        // Grants delivered straight through OnPickupDone (no Pickup entity, no ShopMenu) - the train-ticket
        // machine - never reach the pickup/shop detect hooks, so send the check here. Idempotent: the bridge
        // dedups, so the world-pickup (dummy itemType, excluded above) and Windows-shop paths can't double-send.
        if (g_report_collected)
            g_report_collected(slot);
        return true;
    }
    return false;
}

// Items::OnPickup runs before OnPickupDone and, for armor upgrades (Vitality Vest 0x4f, Damage armor 0x50),
// ORs the upgrade bit into SaveSlot+0xc68 *itself* - so suppressing only OnPickupDone leaks the vanilla
// armor for an AP shop buy (issue #71). Suppress those armor types for AP locations here, at the real
// chokepoint. Scoped to the two armor itemTypes so every other pickup flows through unchanged (OnPickupDone
// still does the per-location suppression for them). Idempotent collect-report mirrors OnPickupDone.
bool on_items_pickup(int slot, int item_type, void *player)
{
    if (g_tracker != nullptr)
        g_tracker->note_player(player);

    // Backstop to TicketMachineGate: the machine should never have been interactible, so reaching here
    // means the prompt suppression missed. Refusing the grant costs the player the donated bones, which
    // is why it is the second line of defence and not the first.
    if (g_train_pass_machine_blocked && mth::is_train_pass_machine_grant(slot, item_type))
    {
        pal::logf(pal::LogLevel::Warn, "train: refused the donation machine's Train Pass grant (#162)");
        return true;
    }

    if (slot >= 0 && mth::tables::is_armor_upgrade_itemtype(item_type) && g_is_ap_location && g_is_ap_location(slot))
    {
        pal::logf(pal::LogLevel::Info, "outbound: suppressed vanilla armor upgrade for AP location %d (itemType=%d)", slot, item_type);
        if (g_report_collected)
            g_report_collected(slot);
        return true;
    }
    return false;
}

} // namespace

namespace mth
{

void set_train_pass_machine_blocked(bool on)
{
    g_train_pass_machine_blocked = on;
}

ItemGranter::ItemGranter(PlayerTracker &tracker, std::function<bool(int)> is_ap_location, std::function<void(int)> report_collected)
{
    g_tracker = &tracker;
    g_is_ap_location = std::move(is_ap_location);
    g_report_collected = std::move(report_collected);
    g_call_on_pickup_done = reinterpret_cast<void (*)(int, int, void *, void *, int, int, unsigned int, bool)>(pal::resolve_game_symbol(sym::on_pickup_done));
    if (g_call_on_pickup_done == nullptr)
        pal::logf(pal::LogLevel::Warn, "ItemGranter: Items::OnPickupDone not resolved; inbound grants disabled");
    mod::install_items_on_pickup_done_hook(&on_items_pickup_done);
    mod::install_items_on_pickup_hook(&on_items_pickup);
}

ItemGranter::~ItemGranter()
{
    mod::remove_items_on_pickup_hook();
    mod::remove_items_on_pickup_done_hook();
    g_call_on_pickup_done = nullptr;
    std::lock_guard<std::mutex> lk(g_pending_mtx);
    g_pending.clear();
    g_tracker = nullptr;
    g_is_ap_location = nullptr;
    g_report_collected = nullptr;
    g_train_pass_machine_blocked = false;
}

bool ItemGranter::grant(int item_type, int receipt)
{
    // Require hook + Player* before accepting; return false to retry next tick.
    if (g_call_on_pickup_done == nullptr || g_tracker == nullptr || g_tracker->player() == nullptr)
        return false;

    // itemType 0 = engine "None" sentinel; nothing to apply, so ack straight away.
    if (item_type <= 0)
    {
        notify_applied(receipt);
        return true;
    }

    std::lock_guard<std::mutex> lk(g_pending_mtx);
    g_pending.push_back(PendingGrant{item_type, receipt});
    return true;
}

// Session change: the queue belongs to the old save. Dropping it un-acked makes the caller re-offer
// each receipt against the new one, which is the only safe direction (#175).
void ItemGranter::discard_pending()
{
    std::lock_guard<std::mutex> lk(g_pending_mtx);
    if (!g_pending.empty())
        pal::logf(pal::LogLevel::Info, "inbound: dropped %zu queued grant(s) on session change", g_pending.size());
    g_pending.clear();
}

void ItemGranter::drain()
{
    std::vector<PendingGrant> batch;
    {
        std::lock_guard<std::mutex> lk(g_pending_mtx);
        if (g_pending.empty())
            return;
        batch.swap(g_pending);
    }

    // Position comes from the tracker's in-context cache; PlayerGetPos3 dereferences an unwired
    // second-level pointer unconditionally, which is null pre-World::Update (spawn window) and
    // would fault. Requeue until cached.
    float p[3];
    void *player = g_tracker != nullptr ? g_tracker->player() : nullptr;
    const bool ready = g_call_on_pickup_done && player != nullptr && g_tracker->position(p);
    if (!ready || !std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]))
    {
        std::lock_guard<std::mutex> lk(g_pending_mtx);
        g_pending.insert(g_pending.begin(), batch.begin(), batch.end());
        return;
    }

    // locIdx=-1: grant by type only, no location state touched.
    for (const PendingGrant &pending : batch)
    {
        YcVec3 grant_pos{p[0], p[1], p[2]}; // fresh copy per item (ycVec3 const&)
        g_call_on_pickup_done(-1, pending.item_type, player, &grant_pos, 0, 0, 0, false);
        pal::logf(pal::LogLevel::Info, "inbound: granted item_type=%d (kind=%d)", pending.item_type, tables::storage_kind(pending.item_type));
        // Ack only now, once the item is really in the save. Everything upstream of this keys its
        // durable state off the ack, so a batch that never reaches here is retried, not lost (#175).
        notify_applied(pending.receipt);
    }
}

} // namespace mth
