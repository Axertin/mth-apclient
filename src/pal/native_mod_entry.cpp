#include <cstdint>

#include "MinaModAPI.h"
#include "pal/pal_crash.hpp"
#include "pal/pal_entry.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"
#include "pal/pal_thread.hpp"

#if defined(_WIN32)
#define MM_EXPORT __declspec(dllexport)
#else
#define MM_EXPORT __attribute__((visibility("default")))
#endif

namespace
{

MinaModAPI *g_mod_api = nullptr;

void apclient_main_trampoline(void * /*arg*/)
{
    pal::apclient_main();
}

// ---- Items::IsItemCollected override via the native modding hook ("IsItemCollected") ----
// The game's Items::IsItemCollected calls MinaModLoader::RunHooks("IsItemCollected", &ctx) at its top,
// so a registered hook overrides the result on BOTH platforms and -- unlike the old symbol/sig detour --
// also fires from every inlined copy of the function (the clang-cl-inlined Pickup-ctor self-kill, etc.).
// The ctx layout below MUST mirror the game's modHookCtx struct exactly.
struct IsItemCollectedCtx
{
    void *collection;             // ItemCollection*
    void *save_slot;              // SaveSlot*
    std::int32_t index;           // location index
    bool include_pawn_shop;       // param4
    bool include_early_collected; // param5: the "ownership query" flag (weapon-swap chest reads with this true)
    bool mod_handled;             // out: true => game returns mod_ret_val instead of running its own logic
    bool mod_ret_val;             // out
};

pal::ItemCollectedFn g_item_collected_cb = nullptr;
void *g_item_collected_handle = nullptr;

void is_item_collected_trampoline(void *pctx)
{
    auto *c = static_cast<IsItemCollectedCtx *>(pctx);
    if (g_item_collected_cb == nullptr || c == nullptr || c->index < 0)
        return;
    const int ov = g_item_collected_cb(c->index, c->include_early_collected); // -1 pass through, 0/1 force
    if (ov >= 0)
    {
        c->mod_handled = true;
        c->mod_ret_val = (ov != 0);
    }
}

// ---- World::Update pre-tick via the native "WorldUpdate" mod hook ----
// ctx points at the World* (unused here -- our sink takes no args), so it is ignored.
pal::WorldUpdatePreFn g_world_update_cb = nullptr;
void *g_world_update_handle = nullptr;

void world_update_trampoline(void * /*pctx*/)
{
    if (g_world_update_cb != nullptr)
        g_world_update_cb();
}

} // namespace

namespace pal
{

bool install_item_collected_hook(ItemCollectedFn query)
{
    if (g_mod_api == nullptr || g_mod_api->InstallHook == nullptr)
    {
        logf(LogLevel::Warn, "items: modding API unavailable; IsItemCollected override disabled");
        return false;
    }
    g_item_collected_cb = query;
    g_item_collected_handle = g_mod_api->InstallHook("IsItemCollected", 0, &is_item_collected_trampoline);
    if (g_item_collected_handle == nullptr)
    {
        logf(LogLevel::Warn, "items: InstallHook(IsItemCollected) returned null; override disabled");
        g_item_collected_cb = nullptr;
        return false;
    }
    logf(LogLevel::Info, "items: IsItemCollected override installed (modding hook)");
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
        logf(LogLevel::Warn, "world: modding API unavailable; World::Update pre-hook disabled");
        return false;
    }
    g_world_update_cb = on_pre;
    g_world_update_handle = g_mod_api->InstallHook("WorldUpdate", 0, &world_update_trampoline);
    if (g_world_update_handle == nullptr)
    {
        logf(LogLevel::Warn, "world: InstallHook(WorldUpdate) returned null; pre-hook disabled");
        g_world_update_cb = nullptr;
        return false;
    }
    logf(LogLevel::Info, "world: World::Update pre-hook installed (modding hook)");
    return true;
}

void remove_world_update_hook()
{
    if (g_world_update_handle != nullptr && g_mod_api != nullptr && g_mod_api->RemoveHook != nullptr)
        g_mod_api->RemoveHook(g_world_update_handle);
    g_world_update_handle = nullptr;
    g_world_update_cb = nullptr;
}

std::uint32_t game_revision()
{
    return (g_mod_api != nullptr && g_mod_api->GetGameRevision != nullptr) ? g_mod_api->GetGameRevision() : 0;
}

} // namespace pal

// Entry point the native mod loader calls.
extern "C" MM_EXPORT void MinaMod_Init(MinaModAPI *mm)
{
    g_mod_api = mm;
    pal::log_init();
    pal::install_crash_handler(); // before any mod work, so it catches everything
    if (mm && mm->APIVersion != MinaModAPI_Version)
        pal::logf(pal::LogLevel::Warn, "MinaModAPI version mismatch (expected %u, got %zu); proceeding anyway", static_cast<unsigned>(MinaModAPI_Version),
                  static_cast<size_t>(mm->APIVersion));
    pal::spawn_thread("mthap-main", &apclient_main_trampoline, nullptr);
}
