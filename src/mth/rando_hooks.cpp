#include "mth/rando_hooks.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "mth/core/game_symbols.hpp"
#include "mth/core/rando_bridge.hpp"
#include "mth/game_item_granter.hpp"
#include "pal/pal_hook.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

namespace
{

mth::RandoBridge *g_bridge = nullptr;
pal::HookId g_id = pal::kInvalidHookId;

// void Items::OnPickupDone(int slot, int itemType, Player*, ycVec3 const&,
//                          int, int, unsigned int, bool)
void (*g_orig_on_pickup_done)(int, int, void *, void *, int, int, unsigned int, bool) = nullptr;

// --- Live Player* + world position, for replaying inbound AP item grants ----------
// OnPickupDone needs the player and a real in-world position. We source both entirely
// by symbol, with NO hardcoded struct offsets:
//   - g_player: the Player object, captured at its construction (available frame 1) and
//     refreshed by the pickup detour.
//   - g_trackable: the player's PlayerTrackable, refreshed each frame from its Update.
//   - g_get_pos: PlayerTrackable::GetPos(), the engine's canonical live-position accessor
//     (what the camera tracks). Returns a ycVec3 (3 floats) by value in registers.
// All game-thread-only.
struct YcVec3
{
    float x, y, z;
};

void *g_player = nullptr;
void *g_trackable = nullptr;
YcVec3 (*g_get_pos)(const void *) = nullptr;

// Base of the game's s_rItems table (195 entries x 0x68 bytes; storage-kind int at +0x28).
// Only used to log the item's kind alongside each grant (the drain path itself is kind-
// agnostic; OnPickupDone handles every kind from the spawn-safe window).
std::uintptr_t g_s_r_items = 0;
constexpr int kItemEntryStride = 0x68;
constexpr int kStorageKindOffset = 0x28;
constexpr int kItemTypeCount = 195;

// Pickup entity field offsets (BUILD-SPECIFIC; the only non-symbol dependency). locIdx is the
// s_rItemCollection index; itemType is the effective spawned type. Verified by a startup
// self-check (repl_pickup_init compares the stored locIdx to the Init arg); on mismatch we
// disable the outbound redirect rather than corrupt pickups.
constexpr std::ptrdiff_t kPickupLocIdxOff = 0x380;
constexpr std::ptrdiff_t kPickupItemTypeOff = 0x384;
bool g_pickup_offsets_ok = true; // cleared by the self-check on mismatch

[[nodiscard]] int &pickup_loc_idx(void *self)
{
    return *reinterpret_cast<int *>(static_cast<char *>(self) + kPickupLocIdxOff);
}
[[nodiscard]] int &pickup_item_type(void *self)
{
    return *reinterpret_cast<int *>(static_cast<char *>(self) + kPickupItemTypeOff);
}

// The AP sentinel: kItemType_Magic_Small (37) -- secondary-weapon ammo with self-contained
// valid assets; the least-valuable grantable item, so the native collect is harmless.
constexpr int kApSentinelItemType = 37;

// Pending inbound-grant queue.
// grant() (called from the console present-hook thread AND the game thread via InboundGranter)
// only ENQUEUES; drain() (called from a spawn-safe PRE-orig update hook, game thread) replays
// the grants. Decoupling the two lets the actual world-entity spawn happen inside the engine's
// own update window, which is what real pickups do and what avoids the update-queue hang.
std::mutex g_pending_mtx;
std::vector<int> g_pending;

[[nodiscard]] int storage_kind(int item_type)
{
    if (item_type < 0 || item_type >= kItemTypeCount || g_s_r_items == 0)
        return -1; // out of range or unclassifiable
    return *reinterpret_cast<const int *>(g_s_r_items + static_cast<std::uintptr_t>(item_type) * kItemEntryStride + kStorageKindOffset);
}

pal::HookId g_id_player_ctor = pal::kInvalidHookId;
pal::HookId g_id_trackable_update = pal::kInvalidHookId;
void (*g_orig_player_ctor)(void *, void *, void *, void *) = nullptr;
void (*g_orig_trackable_update)(void *, void *) = nullptr;

void repl_player_ctor(void *self, void *entity, void *desc, void *setup)
{
    if (g_orig_player_ctor)
        g_orig_player_ctor(self, entity, desc, setup);
    g_player = self; // the constructed Player; available before any pickup
}

void repl_trackable_update(void *self, void *ctx)
{
    g_trackable = self; // refresh each frame -> always the live trackable
    if (g_orig_trackable_update)
        g_orig_trackable_update(self, ctx);
}

void repl_on_pickup_done(int slot, int item_type, void *player, void *vec, int a5, int a6, unsigned int a7, bool a8)
{
    g_player = player; // refresh the grant-target player for inbound replays
    if (g_orig_on_pickup_done)
        g_orig_on_pickup_done(slot, item_type, player, vec, a5, a6, a7, a8);
}

pal::HookId g_id_pickup_init = pal::kInvalidHookId;
void (*g_orig_pickup_init)(void *, int, int) = nullptr;

void repl_pickup_init(void *self, int item_type, int loc_idx)
{
    if (g_orig_pickup_init)
        g_orig_pickup_init(self, item_type, loc_idx); // entity now holds locIdx + derived type

    // Offset self-check (once-effective): the just-constructed entity must store loc_idx at
    // our locIdx offset. If not, the layout shifted (nightly update) -> disable the redirect.
    if (g_pickup_offsets_ok && pickup_loc_idx(self) != loc_idx)
    {
        g_pickup_offsets_ok = false;
        pal::logf(pal::LogLevel::Error, "Pickup offset check FAILED (stored=%d arg=%d); outbound redirect disabled", pickup_loc_idx(self), loc_idx);
        return;
    }
    if (!g_pickup_offsets_ok || g_bridge == nullptr)
        return;

    if (g_bridge->is_ap_location(loc_idx))
        pickup_item_type(self) = kApSentinelItemType; // show the harmless sentinel in-world
}

pal::HookId g_id_pickup_on_pickup = pal::kInvalidHookId;
void (*g_orig_pickup_on_pickup)(void *, void *) = nullptr;

void repl_pickup_on_pickup(void *self, void *listener)
{
    if (g_pickup_offsets_ok && g_bridge != nullptr)
    {
        const int loc_idx = pickup_loc_idx(self);
        if (g_bridge->is_ap_location(loc_idx))
        {
            pal::logf(pal::LogLevel::Info, "outbound: collected AP location locIdx=%d", loc_idx);
            g_bridge->on_location_collected(loc_idx); // mark_checked + persist + send if connected
        }
    }
    // PATH A: forward the native collect. The sentinel grants a sliver of magic, the game sets
    // its own collected-bit (locIdx) -> no respawn, and the pickup tears down normally.
    if (g_orig_pickup_on_pickup)
        g_orig_pickup_on_pickup(self, listener);
}

// Resolve + install a hook by symbol, logging the outcome. Returns the id (or invalid).
pal::HookId hook_by_symbol(const char *symbol, void *replacement, void **trampoline, const char *label)
{
    const auto addr = pal::resolve_game_symbol(symbol);
    if (addr == 0)
    {
        pal::logf(pal::LogLevel::Warn, "RandoHooks: symbol %s (%s) not found; not hooked", symbol, label);
        return pal::kInvalidHookId;
    }
    void *target = reinterpret_cast<void *>(addr);
    const auto id = pal::hook_engine().install_hook(target, replacement, trampoline);
    if (id == pal::kInvalidHookId)
        pal::logf(pal::LogLevel::Error, "RandoHooks: failed to hook %s at %p", label, target);
    else
        pal::logf(pal::LogLevel::Info, "RandoHooks: hooked %s at %p (id=%llu)", label, target, static_cast<unsigned long long>(id));
    return id;
}

} // namespace

namespace mth
{

RandoHooks::RandoHooks(RandoBridge &bridge)
{
    g_bridge = &bridge;

    g_id =
        hook_by_symbol(sym::on_pickup_done, reinterpret_cast<void *>(&repl_on_pickup_done), reinterpret_cast<void **>(&g_orig_on_pickup_done), "OnPickupDone");
    if (g_id == pal::kInvalidHookId)
    {
        g_bridge = nullptr;
        return;
    }
    installed_ = true;

    // Player* + live-position plumbing for inbound grants. Best-effort: if any of these
    // is unavailable, grants simply wait (GameItemGranter::grant returns false) -- never
    // crash. Not required for the read-only outbound path above.
    g_id_player_ctor =
        hook_by_symbol(sym::player_ctor, reinterpret_cast<void *>(&repl_player_ctor), reinterpret_cast<void **>(&g_orig_player_ctor), "Player::Player");
    g_id_trackable_update = hook_by_symbol(sym::player_trackable_update, reinterpret_cast<void *>(&repl_trackable_update),
                                           reinterpret_cast<void **>(&g_orig_trackable_update), "PlayerTrackable::Update");
    if (const auto addr = pal::resolve_game_symbol(sym::player_trackable_get_pos); addr != 0)
        g_get_pos = reinterpret_cast<YcVec3 (*)(const void *)>(addr);
    else
        pal::logf(pal::LogLevel::Warn, "RandoHooks: PlayerTrackable::GetPos not found; inbound grants disabled");

    g_s_r_items = pal::resolve_game_symbol(sym::s_r_items);
    if (g_s_r_items == 0)
        pal::logf(pal::LogLevel::Warn, "RandoHooks: s_rItems not found; item kind will log as -1 (grants still work)");

    g_id_pickup_init =
        hook_by_symbol(sym::pickup_init, reinterpret_cast<void *>(&repl_pickup_init), reinterpret_cast<void **>(&g_orig_pickup_init), "Pickup::Init");
    g_id_pickup_on_pickup = hook_by_symbol(sym::pickup_on_pickup, reinterpret_cast<void *>(&repl_pickup_on_pickup),
                                           reinterpret_cast<void **>(&g_orig_pickup_on_pickup), "Pickup::OnPickup");
}

RandoHooks::~RandoHooks()
{
    if (g_id_trackable_update != pal::kInvalidHookId)
        pal::hook_engine().remove_hook(g_id_trackable_update);
    if (g_id_player_ctor != pal::kInvalidHookId)
        pal::hook_engine().remove_hook(g_id_player_ctor);
    if (g_id_pickup_init != pal::kInvalidHookId)
        pal::hook_engine().remove_hook(g_id_pickup_init);
    if (g_id_pickup_on_pickup != pal::kInvalidHookId)
        pal::hook_engine().remove_hook(g_id_pickup_on_pickup);
    if (installed_)
        pal::hook_engine().remove_hook(g_id);
    g_bridge = nullptr;
}

bool GameItemGranter::grant(int item_type)
{
    // ENQUEUE ONLY. Need the hook, a Player*, and the live position accessor + trackable
    // before we accept the item (the player comes from its ctor on frame 1; the position
    // from PlayerTrackable::GetPos()). Until those exist, return false so the InboundGranter
    // retries on a later tick rather than marking the item granted. The actual replay
    // happens in drain(), on a spawn-safe game-thread hook.
    if (g_orig_on_pickup_done == nullptr || g_player == nullptr || g_trackable == nullptr || g_get_pos == nullptr)
        return false;

    // itemType 0 is the engine's "None" sentinel (ap_item_id 0). Nothing to grant; treat as
    // handled so the inbound cursor advances past it rather than retrying forever.
    if (item_type <= 0)
        return true;

    std::lock_guard<std::mutex> lk(g_pending_mtx);
    g_pending.push_back(item_type);
    return true;
}

void GameItemGranter::drain()
{
    std::vector<int> batch;
    {
        std::lock_guard<std::mutex> lk(g_pending_mtx);
        if (g_pending.empty())
            return;
        batch.swap(g_pending);
    }

    // Readiness can lapse between enqueue and drain (e.g. player destroyed on a level change),
    // and the live position must be finite. Either way, requeue and wait for the next drain
    // rather than replaying with a stale Player* or at a garbage position.
    const bool ready = g_orig_on_pickup_done && g_player && g_trackable && g_get_pos;
    const YcVec3 pos = ready ? g_get_pos(g_trackable) : YcVec3{};
    if (!ready || !std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z))
    {
        std::lock_guard<std::mutex> lk(g_pending_mtx);
        g_pending.insert(g_pending.begin(), batch.begin(), batch.end());
        return;
    }

    // Replay the game's own award path per queued item. locIdx = -1 grants by type only (no
    // location state touched); running here, inside the engine's update window, is what
    // lets spawning kinds (money FX / subweapon / magic) spawn safely.
    for (int item_type : batch)
    {
        YcVec3 grant_pos = pos; // fresh copy per item (OnPickupDone takes ycVec3 const&)
        g_orig_on_pickup_done(-1, item_type, g_player, &grant_pos, 0, 0, 0, false);
        pal::logf(pal::LogLevel::Info, "inbound: granted item_type=%d (kind=%d)", item_type, storage_kind(item_type));
    }
}

} // namespace mth
