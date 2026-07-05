#include "mod/mod_api.hpp"

#include "MinaModAPI.h"
#include "pal/pal_log.hpp"

namespace
{

MinaModAPI *g_mod_api = nullptr;

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

} // namespace

namespace mod
{

void set_api(MinaModAPI *api)
{
    g_mod_api = api;
}

std::uint32_t game_revision()
{
    return (g_mod_api != nullptr && g_mod_api->GetGameRevision != nullptr) ? g_mod_api->GetGameRevision() : 0;
}

bool install_item_collected_hook(ItemCollectedFn query)
{
    if (g_mod_api == nullptr || g_mod_api->InstallHook == nullptr)
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
    if (g_item_collected_handle != nullptr && g_mod_api != nullptr && g_mod_api->RemoveHook != nullptr)
        g_mod_api->RemoveHook(g_item_collected_handle);
    g_item_collected_handle = nullptr;
    g_item_collected_cb = nullptr;
}

bool install_world_update_hook(WorldUpdatePreFn on_pre)
{
    if (g_mod_api == nullptr || g_mod_api->InstallHook == nullptr)
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
    if (g_world_update_handle != nullptr && g_mod_api != nullptr && g_mod_api->RemoveHook != nullptr)
        g_mod_api->RemoveHook(g_world_update_handle);
    g_world_update_handle = nullptr;
    g_world_update_cb = nullptr;
}

} // namespace mod
