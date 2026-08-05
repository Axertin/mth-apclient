#include "mod/mod_api.hpp"

#include "MinaModAPI.h"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

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

mod::ItemCollectedFn g_item_collected_cb = nullptr;
void *g_item_collected_handle = nullptr;

void is_item_collected_trampoline(void *pctx)
{
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
void *g_world_update_handle = nullptr;

void world_update_trampoline(void * /*pctx*/)
{
    if (g_world_update_cb != nullptr)
        g_world_update_cb();
}

mod::WorldDestroyFn g_world_destroy_cb = nullptr;
void *g_world_destroy_handle = nullptr;

void world_destroy_trampoline(void * /*pctx*/)
{
    if (g_world_destroy_cb != nullptr)
        g_world_destroy_cb();
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
    g_item_collected_handle = g_mod_api->InstallHook("IsItemCollected", 0, &is_item_collected_trampoline);
    if (g_item_collected_handle == nullptr)
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
    if (g_item_collected_handle != nullptr && g_mod_api != nullptr && usable(g_mod_api->RemoveHook))
        g_mod_api->RemoveHook(g_item_collected_handle);
    g_item_collected_handle = nullptr;
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
    g_world_update_handle = g_mod_api->InstallHook("WorldUpdate", 0, &world_update_trampoline);
    if (g_world_update_handle == nullptr)
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
    if (g_world_update_handle != nullptr && g_mod_api != nullptr && usable(g_mod_api->RemoveHook))
        g_mod_api->RemoveHook(g_world_update_handle);
    g_world_update_handle = nullptr;
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
    g_world_destroy_handle = g_mod_api->InstallHook("WorldDestroy", 0, &world_destroy_trampoline);
    if (g_world_destroy_handle == nullptr)
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
    if (g_world_destroy_handle != nullptr && g_mod_api != nullptr && usable(g_mod_api->RemoveHook))
        g_mod_api->RemoveHook(g_world_destroy_handle);
    g_world_destroy_handle = nullptr;
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
