#include "mod/mod_api.hpp"

#include <atomic>
#include <cstddef>
#include <cstring>
#include <utility>

#include "MinaModAPI.h"
#include "MinaModHooks.h"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

// mod::IsItemCollectedCtx mirrors the game's hook context by hand so the public header stays free of
// game headers. Upstream now ships the real struct, so the mirror is checked here instead of by eye.
static_assert(sizeof(mod::IsItemCollectedCtx) == sizeof(::IsItemCollectedCtx));
static_assert(offsetof(mod::IsItemCollectedCtx, collection) == offsetof(::IsItemCollectedCtx, collection));
static_assert(offsetof(mod::IsItemCollectedCtx, save_slot) == offsetof(::IsItemCollectedCtx, saveSlot));
static_assert(offsetof(mod::IsItemCollectedCtx, index) == offsetof(::IsItemCollectedCtx, index));
static_assert(offsetof(mod::IsItemCollectedCtx, include_pawn_shop) == offsetof(::IsItemCollectedCtx, includePawnShop));
static_assert(offsetof(mod::IsItemCollectedCtx, include_early_collected) == offsetof(::IsItemCollectedCtx, includeEarlyCollected));
static_assert(offsetof(mod::IsItemCollectedCtx, mod_handled) == offsetof(::IsItemCollectedCtx, modHandled));
static_assert(offsetof(mod::IsItemCollectedCtx, mod_ret_val) == offsetof(::IsItemCollectedCtx, modRetVal));

// The six floats the AABB wrappers memcpy are center.xyz then half-extent.xyz, which only holds if
// MM_AABB is exactly that and nothing else.
static_assert(sizeof(MM_AABB) == 6 * sizeof(float));
static_assert(offsetof(MM_AABB, center) == 0);
static_assert(offsetof(MM_AABB, extents) == 3 * sizeof(float));

namespace
{

constexpr int kSaveSlotCount = 10;

MinaModAPI *g_mod_api = nullptr;

// An entry read past the end of a shorter struct is not null, it is whatever static follows the
// API table, so a null check alone does not prove the pointer is callable.
template <class Fn> bool usable(Fn fn)
{
    return fn != nullptr && pal::in_game_text(reinterpret_cast<const void *>(fn));
}

// Newest game build known to predate the 2026-08-05 appended API entries.
constexpr std::uint32_t kNewestRevisionWithoutAppendedApi = 148905;

// Test this before reading any appended field. On an older build those fields sit past the end of the
// game's shorter struct, and what follows the struct is more relocated function pointers, so an
// address test alone would accept garbage; the read itself is what has to be avoided. Upstream
// appended the fields without bumping MinaModAPI_Version, leaving the revision as the only oracle.
bool appended_api_possible()
{
    return g_mod_api != nullptr && usable(g_mod_api->GetGameRevision) && g_mod_api->GetGameRevision() > kNewestRevisionWithoutAppendedApi;
}

// For an appended entry, once appended_api_possible() has cleared the read. Stricter than usable():
// a field that may not exist cannot be checked without a range, so this does not fail open.
template <class Fn> bool usable_appended(Fn fn)
{
    return fn != nullptr && pal::text_range_published() && pal::in_game_text(reinterpret_cast<const void *>(fn));
}

// Every named hook the mod installs. InstallHook accepts any string and returns a valid handle
// whether or not the build dispatches that name, and RunHooks ignores a hash it does not know, so
// neither the handle nor RemoveHook proves a hook exists. Firing is the only evidence, so each
// trampoline records it and unfired_hooks() reports the rest.
enum HookId
{
    kHookIsItemCollected,
    kHookWorldUpdate,
    kHookWorldDestroy,
    kHookItemsOnPickup,
    kHookItemsOnPickupDone,
    kHookPickupOnPickup,
    kHookShopItemRefresh,
    kHookAreaManagerNewArea,
    kHookCount,
};

constexpr const char *kHookNames[kHookCount] = {
    "IsItemCollected", "WorldUpdate", "WorldDestroy", "ItemsOnPickup", "ItemsOnPickupDone", "PickupOnPickup", "ShopItemRefresh", "AreaManagerNewArea",
};

std::atomic<bool> g_hook_fired[kHookCount];
void *g_hook_handles[kHookCount];

void note_fired(HookId id)
{
    if (!g_hook_fired[id].exchange(true))
        pal::logf(pal::LogLevel::Info, "mod hook: \"%s\" fired on this build", kHookNames[id]);
}

// Null when the API is unusable. A non-null handle says nothing about whether the name dispatches.
void *install_named(HookId id, MM_HookCallback cb)
{
    if (g_mod_api == nullptr || !usable(g_mod_api->InstallHook))
        return nullptr;
    g_hook_handles[id] = g_mod_api->InstallHook(kHookNames[id], 0, cb);
    return g_hook_handles[id];
}

mod::ItemCollectedFn g_item_collected_cb = nullptr;

void is_item_collected_trampoline(void *pctx)
{
    note_fired(kHookIsItemCollected);
    auto *c = static_cast<mod::IsItemCollectedCtx *>(pctx);
    if (g_item_collected_cb == nullptr || c == nullptr || c->index < 0)
        return;
    const int ov = g_item_collected_cb(c->index, c->include_early_collected); // -1 pass through, 0/1 force
    if (ov >= 0)
    {
        c->mod_handled = true;
        c->mod_ret_val = (ov != 0);
    }
}

mod::WorldUpdatePreFn g_world_update_cb = nullptr;

void world_update_trampoline(void * /*pctx*/)
{
    note_fired(kHookWorldUpdate);
    if (g_world_update_cb != nullptr)
        g_world_update_cb();
}

mod::WorldDestroyFn g_world_destroy_cb = nullptr;

void world_destroy_trampoline(void * /*pctx*/)
{
    note_fired(kHookWorldDestroy);
    if (g_world_destroy_cb != nullptr)
        g_world_destroy_cb();
}

mod::ItemsOnPickupFn g_items_on_pickup_cb = nullptr;

void items_on_pickup_trampoline(void *pctx)
{
    note_fired(kHookItemsOnPickup);
    auto *c = static_cast<ItemsOnPickupCtx *>(pctx);
    if (g_items_on_pickup_cb == nullptr || c == nullptr || c->collectionIndex == nullptr || c->itemType == nullptr)
        return;
    void *player = (c->ppPlayer != nullptr) ? static_cast<void *>(*c->ppPlayer) : nullptr;
    if (g_items_on_pickup_cb(*c->collectionIndex, *c->itemType, player))
        c->modHandled = true;
}

mod::ItemsOnPickupDoneFn g_items_on_pickup_done_cb = nullptr;

void items_on_pickup_done_trampoline(void *pctx)
{
    note_fired(kHookItemsOnPickupDone);
    auto *c = static_cast<ItemsOnPickupDoneCtx *>(pctx);
    if (g_items_on_pickup_done_cb == nullptr || c == nullptr || c->collectionIndex == nullptr || c->itemType == nullptr)
        return;
    void *player = (c->ppPlayer != nullptr) ? static_cast<void *>(*c->ppPlayer) : nullptr;
    if (g_items_on_pickup_done_cb(*c->collectionIndex, *c->itemType, player))
        c->modHandled = true;
}

mod::PickupOnPickupFn g_pickup_on_pickup_cb = nullptr;

void pickup_on_pickup_trampoline(void *pctx)
{
    note_fired(kHookPickupOnPickup);
    auto *c = static_cast<PickupOnPickupCtx *>(pctx);
    // The ctx carries the real Pickup*, so no per-platform subobject fixup is needed here.
    if (g_pickup_on_pickup_cb != nullptr && c != nullptr && c->pickup != nullptr)
        g_pickup_on_pickup_cb(static_cast<void *>(c->pickup));
}

mod::ShopItemRefreshFn g_shop_item_refresh_cb = nullptr;

void shop_item_refresh_trampoline(void *pctx)
{
    note_fired(kHookShopItemRefresh);
    auto *c = static_cast<ShopItemRefreshCtx *>(pctx);
    if (g_shop_item_refresh_cb != nullptr && c != nullptr && c->shopItem != nullptr)
        g_shop_item_refresh_cb(static_cast<void *>(c->shopItem));
}

mod::AreaNewAreaFn g_area_new_area_cb = nullptr;

void area_new_area_trampoline(void *pctx)
{
    note_fired(kHookAreaManagerNewArea);
    auto *c = static_cast<AreaManagerNewAreaCtx *>(pctx);
    if (g_area_new_area_cb == nullptr || c == nullptr || c->oldArea == nullptr || c->newArea == nullptr)
        return;
    g_area_new_area_cb(*c->oldArea, *c->newArea);
}

} // namespace

namespace mod
{

void set_api(MinaModAPI *api)
{
    g_mod_api = api;
}

bool api_available()
{
    return g_mod_api != nullptr && usable(g_mod_api->InstallHook);
}

bool api_version_matches()
{
    return g_mod_api != nullptr && g_mod_api->APIVersion == MinaModAPI_Version;
}

std::uint32_t expected_api_version()
{
    return static_cast<std::uint32_t>(MinaModAPI_Version);
}

std::uint32_t game_revision()
{
    return (g_mod_api != nullptr && usable(g_mod_api->GetGameRevision)) ? g_mod_api->GetGameRevision() : 0;
}

int current_game_state()
{
    return (g_mod_api != nullptr && usable(g_mod_api->GetCurrentGameState)) ? g_mod_api->GetCurrentGameState() : -1;
}

bool install_item_collected_hook(ItemCollectedFn query)
{
    if (g_mod_api == nullptr || !usable(g_mod_api->InstallHook))
    {
        pal::logf(pal::LogLevel::Warn, "items: modding API unavailable; IsItemCollected override disabled");
        return false;
    }
    g_item_collected_cb = query;
    if (install_named(kHookIsItemCollected, &is_item_collected_trampoline) == nullptr)
    {
        pal::logf(pal::LogLevel::Warn, "items: InstallHook(IsItemCollected) returned null; override disabled");
        g_item_collected_cb = nullptr;
        return false;
    }
    pal::logf(pal::LogLevel::Info, "items: IsItemCollected override installed (modding hook)");
    return true;
}

void remove_item_collected_hook()
{
    if (g_hook_handles[kHookIsItemCollected] != nullptr && g_mod_api != nullptr && usable(g_mod_api->RemoveHook))
        g_mod_api->RemoveHook(g_hook_handles[kHookIsItemCollected]);
    g_hook_handles[kHookIsItemCollected] = nullptr;
    g_item_collected_cb = nullptr;
}

bool install_world_update_hook(WorldUpdatePreFn on_pre)
{
    if (g_mod_api == nullptr || !usable(g_mod_api->InstallHook))
    {
        pal::logf(pal::LogLevel::Warn, "world: modding API unavailable; World::Update pre-hook disabled");
        return false;
    }
    g_world_update_cb = on_pre;
    if (install_named(kHookWorldUpdate, &world_update_trampoline) == nullptr)
    {
        pal::logf(pal::LogLevel::Warn, "world: InstallHook(WorldUpdate) returned null; pre-hook disabled");
        g_world_update_cb = nullptr;
        return false;
    }
    pal::logf(pal::LogLevel::Info, "world: World::Update pre-hook installed (modding hook)");
    return true;
}

void remove_world_update_hook()
{
    if (g_hook_handles[kHookWorldUpdate] != nullptr && g_mod_api != nullptr && usable(g_mod_api->RemoveHook))
        g_mod_api->RemoveHook(g_hook_handles[kHookWorldUpdate]);
    g_hook_handles[kHookWorldUpdate] = nullptr;
    g_world_update_cb = nullptr;
}

bool install_world_destroy_hook(WorldDestroyFn on_destroy)
{
    if (g_mod_api == nullptr || !usable(g_mod_api->InstallHook))
    {
        pal::logf(pal::LogLevel::Warn, "world: modding API unavailable; World::Destroy hook disabled");
        return false;
    }
    g_world_destroy_cb = on_destroy;
    if (install_named(kHookWorldDestroy, &world_destroy_trampoline) == nullptr)
    {
        pal::logf(pal::LogLevel::Warn, "world: InstallHook(WorldDestroy) returned null; teardown hook disabled");
        g_world_destroy_cb = nullptr;
        return false;
    }
    pal::logf(pal::LogLevel::Info, "world: World::Destroy hook installed (modding hook)");
    return true;
}

void remove_world_destroy_hook()
{
    if (g_hook_handles[kHookWorldDestroy] != nullptr && g_mod_api != nullptr && usable(g_mod_api->RemoveHook))
        g_mod_api->RemoveHook(g_hook_handles[kHookWorldDestroy]);
    g_hook_handles[kHookWorldDestroy] = nullptr;
    g_world_destroy_cb = nullptr;
}

bool vial_api_available()
{
    return g_mod_api != nullptr && usable(g_mod_api->PlayerGetMaxVials) && usable(g_mod_api->PlayerSetMaxVials) && usable(g_mod_api->PlayerGetVials) &&
           usable(g_mod_api->PlayerSetVials);
}

int player_max_vials()
{
    return vial_api_available() ? g_mod_api->PlayerGetMaxVials() : 0;
}

int player_vials()
{
    return vial_api_available() ? g_mod_api->PlayerGetVials() : 0;
}

void set_player_max_vials(int n)
{
    if (vial_api_available())
        g_mod_api->PlayerSetMaxVials(n);
}

void set_player_vials(int n)
{
    if (vial_api_available())
        g_mod_api->PlayerSetVials(n);
}

bool bones_api_available()
{
    return g_mod_api != nullptr && usable(g_mod_api->PlayerGetBones) && usable(g_mod_api->PlayerSetBones);
}

int player_bones()
{
    return bones_api_available() ? g_mod_api->PlayerGetBones() : 0;
}

void set_player_bones(int n)
{
    if (bones_api_available())
        g_mod_api->PlayerSetBones(n);
}

bool bosses_api_available()
{
    return g_mod_api != nullptr && usable(g_mod_api->PlayerGetBossesDefeated);
}

std::uint64_t player_bosses_defeated()
{
    return bosses_api_available() ? g_mod_api->PlayerGetBossesDefeated() : 0;
}

bool player_position(float out[3])
{
    if (out == nullptr || g_mod_api == nullptr || !usable(g_mod_api->PlayerGetWorld) || !usable(g_mod_api->PlayerGetPos3))
        return false;
    // PlayerGetPos3 only null-checks the outer component pointer, not the entity pointer one
    // level in; when that second level is unwired it either faults or returns {0,0,0} instead of
    // failing. PlayerGetWorld walks the same chain but checks both levels, so a null return here
    // catches exactly the case PlayerGetPos3 would fault or silently mis-answer on.
    if (g_mod_api->PlayerGetWorld() == nullptr)
        return false;
    const MM_Vec3 v = g_mod_api->PlayerGetPos3();
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
    return true;
}

bool player_die()
{
    if (g_mod_api == nullptr || !usable(g_mod_api->PlayerDie))
        return false;
    g_mod_api->PlayerDie();
    return true;
}

bool player_component_available()
{
    return g_mod_api != nullptr && usable(g_mod_api->PlayerGetComponent);
}

void *player_component()
{
    return player_component_available() ? static_cast<void *>(g_mod_api->PlayerGetComponent()) : nullptr;
}

bool health_api_available()
{
    return g_mod_api != nullptr && usable(g_mod_api->PlayerGetHealth);
}

float player_health()
{
    return health_api_available() ? g_mod_api->PlayerGetHealth() : 0.0f;
}

bool spark_api_available()
{
    return g_mod_api != nullptr && usable(g_mod_api->PlayerGetSpark);
}

int player_spark()
{
    return spark_api_available() ? g_mod_api->PlayerGetSpark() : 0;
}

bool pause_api_available()
{
    return g_mod_api != nullptr && usable(g_mod_api->PlayerGetWorld) && usable(g_mod_api->WorldIsPaused);
}

bool world_is_paused()
{
    if (!pause_api_available())
        return false;
    World *w = g_mod_api->PlayerGetWorld(); // null with no live player; WorldIsPaused would dereference it
    return w != nullptr && g_mod_api->WorldIsPaused(w);
}

bool room_time_api_available()
{
    return g_mod_api != nullptr && usable(g_mod_api->GetRoomTime);
}

float room_time()
{
    return room_time_api_available() ? g_mod_api->GetRoomTime() : 0.0f;
}

bool save_api_available()
{
    // Only what the takeover actually calls: gating on more would fail a launch that would work.
    return g_mod_api != nullptr && usable(g_mod_api->SetSaveSlotContents) && usable(g_mod_api->GetActiveSaveSlotContents) && usable(g_mod_api->Free) &&
           usable(g_mod_api->SetSaveWriteEnabled) && usable(g_mod_api->IsSaveWriteEnabled);
}

int active_save_slot()
{
    if (g_mod_api == nullptr || !usable(g_mod_api->GetActiveSaveSlot))
        return -1;
    const int slot = g_mod_api->GetActiveSaveSlot();
    // The game's no-slot value is one past the last slot, not a negative sentinel.
    return (slot >= 0 && slot < kSaveSlotCount) ? slot : -1;
}

bool set_active_save_slot_contents(const char *ycdata)
{
    if (ycdata == nullptr || g_mod_api == nullptr || !usable(g_mod_api->SetActiveSaveSlotContents))
        return false;
    return g_mod_api->SetActiveSaveSlotContents(ycdata);
}

bool set_save_slot_contents(unsigned int slot, const char *ycdata)
{
    if (ycdata == nullptr || g_mod_api == nullptr || !usable(g_mod_api->SetSaveSlotContents))
        return false;
    return g_mod_api->SetSaveSlotContents(slot, ycdata);
}

std::string active_save_slot_contents()
{
    if (g_mod_api == nullptr || !usable(g_mod_api->GetActiveSaveSlotContents))
        return {};
    char *raw = g_mod_api->GetActiveSaveSlotContents();
    if (raw == nullptr)
        return {};
    std::string out(raw);
    if (usable(g_mod_api->Free))
        g_mod_api->Free(raw); // the API contract requires freeing through the game's allocator
    return out;
}

void player_restore_from_save()
{
    if (g_mod_api != nullptr && usable(g_mod_api->PlayerRestoreFromSave))
        g_mod_api->PlayerRestoreFromSave();
}

bool install_items_on_pickup_hook(ItemsOnPickupFn on_pickup)
{
    g_items_on_pickup_cb = on_pickup;
    if (install_named(kHookItemsOnPickup, &items_on_pickup_trampoline) == nullptr)
    {
        pal::logf(pal::LogLevel::Warn, "items: InstallHook(ItemsOnPickup) failed; armor-upgrade suppression disabled");
        g_items_on_pickup_cb = nullptr;
        return false;
    }
    return true;
}

bool install_items_on_pickup_done_hook(ItemsOnPickupDoneFn on_done)
{
    g_items_on_pickup_done_cb = on_done;
    if (install_named(kHookItemsOnPickupDone, &items_on_pickup_done_trampoline) == nullptr)
    {
        pal::logf(pal::LogLevel::Warn, "items: InstallHook(ItemsOnPickupDone) failed; grant suppression disabled");
        g_items_on_pickup_done_cb = nullptr;
        return false;
    }
    return true;
}

bool install_pickup_on_pickup_hook(PickupOnPickupFn on_pickup)
{
    g_pickup_on_pickup_cb = on_pickup;
    if (install_named(kHookPickupOnPickup, &pickup_on_pickup_trampoline) == nullptr)
    {
        pal::logf(pal::LogLevel::Warn, "locations: InstallHook(PickupOnPickup) failed; pickup redirect disabled");
        g_pickup_on_pickup_cb = nullptr;
        return false;
    }
    return true;
}

bool install_shop_item_refresh_hook(ShopItemRefreshFn on_refresh)
{
    g_shop_item_refresh_cb = on_refresh;
    if (install_named(kHookShopItemRefresh, &shop_item_refresh_trampoline) == nullptr)
    {
        pal::logf(pal::LogLevel::Warn, "shop: InstallHook(ShopItemRefresh) failed; stock suppression disabled");
        g_shop_item_refresh_cb = nullptr;
        return false;
    }
    return true;
}

bool install_area_new_area_hook(AreaNewAreaFn on_new_area)
{
    g_area_new_area_cb = on_new_area;
    if (install_named(kHookAreaManagerNewArea, &area_new_area_trampoline) == nullptr)
    {
        pal::logf(pal::LogLevel::Warn, "rooms: InstallHook(AreaManagerNewArea) failed; area tracking disabled");
        g_area_new_area_cb = nullptr;
        return false;
    }
    return true;
}

void remove_named(HookId id)
{
    if (g_hook_handles[id] != nullptr && g_mod_api != nullptr && usable(g_mod_api->RemoveHook))
        g_mod_api->RemoveHook(g_hook_handles[id]);
    g_hook_handles[id] = nullptr;
}

void remove_items_on_pickup_hook()
{
    remove_named(kHookItemsOnPickup);
    g_items_on_pickup_cb = nullptr;
}

void remove_items_on_pickup_done_hook()
{
    remove_named(kHookItemsOnPickupDone);
    g_items_on_pickup_done_cb = nullptr;
}

void remove_pickup_on_pickup_hook()
{
    remove_named(kHookPickupOnPickup);
    g_pickup_on_pickup_cb = nullptr;
}

void remove_shop_item_refresh_hook()
{
    remove_named(kHookShopItemRefresh);
    g_shop_item_refresh_cb = nullptr;
}

void remove_area_new_area_hook()
{
    remove_named(kHookAreaManagerNewArea);
    g_area_new_area_cb = nullptr;
}

void remove_all_hooks()
{
    g_items_on_pickup_cb = nullptr;
    g_items_on_pickup_done_cb = nullptr;
    g_pickup_on_pickup_cb = nullptr;
    g_shop_item_refresh_cb = nullptr;
    g_area_new_area_cb = nullptr;
    if (g_mod_api == nullptr || !usable(g_mod_api->RemoveHook))
        return;
    for (void *&h : g_hook_handles)
    {
        if (h != nullptr)
            g_mod_api->RemoveHook(h);
        h = nullptr;
    }
}

std::vector<const char *> unfired_hooks()
{
    std::vector<const char *> out;
    for (int i = 0; i < kHookCount; ++i)
        if (g_hook_handles[i] != nullptr && !g_hook_fired[i].load())
            out.push_back(kHookNames[i]);
    return out;
}

bool queue_destroy_available()
{
    return appended_api_possible() && g_mod_api != nullptr && usable_appended(g_mod_api->WorldQueueDestroyEntity);
}

bool queue_destroy_entity(void *world, void *entity, bool depth_first)
{
    if (world == nullptr || entity == nullptr || !queue_destroy_available())
        return false;
    g_mod_api->WorldQueueDestroyEntity(static_cast<World *>(world), static_cast<ycEntity *>(entity), depth_first);
    return true;
}

bool set_item_collected_available()
{
    return appended_api_possible() && g_mod_api != nullptr && usable_appended(g_mod_api->ItemsSetItemCollected);
}

bool set_item_collected(int index, bool collected, void *collection, void *slot)
{
    if (index < 0 || !set_item_collected_available())
        return false;
    g_mod_api->ItemsSetItemCollected(index, collected, static_cast<ItemCollection *>(collection), static_cast<SaveSlot *>(slot));
    return true;
}

bool text_color_available()
{
    return appended_api_possible() && g_mod_api != nullptr && usable_appended(g_mod_api->TextComponentSetColor);
}

bool set_text_color(void *text_component, std::uint32_t rgba)
{
    if (text_component == nullptr || !text_color_available())
        return false;
    // MM_Color is four uint8 channels in r,g,b,a memory order, i.e. byte-identical to the packed word
    // on little-endian; the direct call passed that word as the by-value ycColor already.
    MM_Color c{};
    c.r = static_cast<std::uint8_t>(rgba & 0xFFu);
    c.g = static_cast<std::uint8_t>((rgba >> 8) & 0xFFu);
    c.b = static_cast<std::uint8_t>((rgba >> 16) & 0xFFu);
    c.a = static_cast<std::uint8_t>((rgba >> 24) & 0xFFu);
    g_mod_api->TextComponentSetColor(static_cast<ycComponent *>(text_component), c);
    return true;
}

bool set_text(void *text_component, const char *utf8)
{
    if (text_component == nullptr || utf8 == nullptr || !appended_api_possible() || g_mod_api == nullptr || !usable_appended(g_mod_api->TextComponentSetText))
        return false;
    g_mod_api->TextComponentSetText(static_cast<ycComponent *>(text_component), utf8);
    return true;
}

const char *text_of(void *text_component)
{
    if (text_component == nullptr || !appended_api_possible() || g_mod_api == nullptr || !usable_appended(g_mod_api->TextComponentGetText))
        return nullptr;
    return g_mod_api->TextComponentGetText(static_cast<ycComponent *>(text_component));
}

bool water_api_available()
{
    return appended_api_possible() && g_mod_api != nullptr && usable_appended(g_mod_api->WaterListenerIsInDeepWater);
}

bool water_is_in_deep_water(void *water_listener, bool ignore_enabled)
{
    if (water_listener == nullptr || !water_api_available())
        return false;
    return g_mod_api->WaterListenerIsInDeepWater(static_cast<WaterListener *>(water_listener), ignore_enabled);
}

bool physics_get_aabb(void *physics_component, float out_aabb[6], bool local, unsigned shape_flags)
{
    if (physics_component == nullptr || out_aabb == nullptr || !appended_api_possible() || g_mod_api == nullptr ||
        !usable_appended(g_mod_api->PhysicsComponentGetAABB))
        return false;
    MM_AABB box{};
    g_mod_api->PhysicsComponentGetAABB(static_cast<PhysicsComponent *>(physics_component), &box, local, shape_flags);
    std::memcpy(out_aabb, &box, sizeof(box));
    return true;
}

void *closest_carryable(void *carry_manager, const float box[6], int collision_layer, float max_dist, int *out_overlap_count, std::uint64_t collide_mask_ignore)
{
    if (carry_manager == nullptr || box == nullptr || !appended_api_possible() || g_mod_api == nullptr ||
        !usable_appended(g_mod_api->CarryManagerGetClosestCarryableObject))
        return nullptr;
    MM_AABB b{};
    std::memcpy(&b, box, sizeof(b));
    return g_mod_api->CarryManagerGetClosestCarryableObject(static_cast<CarryManager *>(carry_manager), b, collision_layer, max_dist, out_overlap_count,
                                                            collide_mask_ignore);
}

bool player_stats_api_available()
{
    return appended_api_possible() && g_mod_api != nullptr && usable_appended(g_mod_api->PlayerUpdateStats);
}

bool player_update_stats()
{
    if (!player_stats_api_available())
        return false;
    g_mod_api->PlayerUpdateStats();
    return true;
}

void *sym_addr(const char *name)
{
    if (!appended_api_possible() || g_mod_api == nullptr || name == nullptr || !usable_appended(g_mod_api->GetSymAddr))
        return nullptr;
    // Not range-checked: the supported names include data symbols that do not live in .text.
    return g_mod_api->GetSymAddr(name);
}

void set_save_write_enabled(bool on)
{
    if (g_mod_api != nullptr && usable(g_mod_api->SetSaveWriteEnabled))
        g_mod_api->SetSaveWriteEnabled(on);
}

bool save_write_enabled()
{
    return (g_mod_api != nullptr && usable(g_mod_api->IsSaveWriteEnabled)) ? g_mod_api->IsSaveWriteEnabled() : false;
}

} // namespace mod
