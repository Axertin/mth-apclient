// The mod-save flush trigger and its directory. The WriteSaveData detour is symbol-resolved through the
// PAL, so nothing here is platform data; new-file staging stays in the platform files because it walks
// g_saveManager, whose master-table offset differs per build.

#include <cstdint>
#include <filesystem>
#include <utility>

#include "mth/core/data/game_symbols.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_hook.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

namespace
{

// SaveManager::WriteSaveData(bool): the flush trigger for mod-owned saves. Observed only, never
// suppressed; the mod's own SetSaveWriteEnabled(false) (via the native MinaModAPI) is what
// actually keeps saveData.yc untouched during a takeover.
pal::SaveRequestedFn g_save_request_fn;
pal::HookId g_save_request_hook = pal::kInvalidHookId;
void (*g_orig_write_save_data)(void *, bool) = nullptr;

void repl_write_save_data(void *self, bool flag)
{
    if (g_save_request_fn)
        g_save_request_fn();
    if (g_orig_write_save_data)
        g_orig_write_save_data(self, flag);
}

} // namespace

namespace pal
{

bool install_save_request_hook(SaveRequestedFn on_save)
{
    g_save_request_fn = std::move(on_save);
    const std::uintptr_t addr = resolve_game_symbol(mth::sym::save_manager_write_save_data);
    if (addr == 0)
    {
        logf(LogLevel::Warn, "takeover: SaveManager::WriteSaveData not resolved; mod saves will not flush");
        g_save_request_fn = nullptr;
        return false;
    }
    g_save_request_hook = hook_engine().install_hook(reinterpret_cast<void *>(addr), reinterpret_cast<void *>(&repl_write_save_data),
                                                     reinterpret_cast<void **>(&g_orig_write_save_data));
    if (g_save_request_hook == kInvalidHookId)
    {
        logf(LogLevel::Error, "takeover: failed to hook SaveManager::WriteSaveData");
        g_save_request_fn = nullptr;
        return false;
    }
    logf(LogLevel::Info, "takeover: hooked SaveManager::WriteSaveData (id=%llu)", static_cast<unsigned long long>(g_save_request_hook));
    return true;
}

void remove_save_request_hook()
{
    if (g_save_request_hook != kInvalidHookId)
        hook_engine().remove_hook(g_save_request_hook);
    g_save_request_hook = kInvalidHookId;
    g_save_request_fn = nullptr;
}

std::filesystem::path mod_save_dir()
{
    return pal::log_dir() / "saves";
}

} // namespace pal
