#include <atomic>
#include <mutex>
#include <unordered_map>

#include "pal/pal_hook.hpp"
#include "pal/pal_log.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <MinHook.h>

namespace
{

bool g_initialized = false;

const char *mh_status_name(MH_STATUS s)
{
    switch (s)
    {
    case MH_OK:
        return "MH_OK";
    case MH_ERROR_ALREADY_INITIALIZED:
        return "MH_ERROR_ALREADY_INITIALIZED";
    case MH_ERROR_NOT_INITIALIZED:
        return "MH_ERROR_NOT_INITIALIZED";
    case MH_ERROR_ALREADY_CREATED:
        return "MH_ERROR_ALREADY_CREATED";
    case MH_ERROR_NOT_CREATED:
        return "MH_ERROR_NOT_CREATED";
    case MH_ERROR_ENABLED:
        return "MH_ERROR_ENABLED";
    case MH_ERROR_DISABLED:
        return "MH_ERROR_DISABLED";
    case MH_ERROR_NOT_EXECUTABLE:
        return "MH_ERROR_NOT_EXECUTABLE";
    case MH_ERROR_UNSUPPORTED_FUNCTION:
        return "MH_ERROR_UNSUPPORTED_FUNCTION";
    case MH_ERROR_MEMORY_ALLOC:
        return "MH_ERROR_MEMORY_ALLOC";
    case MH_ERROR_MEMORY_PROTECT:
        return "MH_ERROR_MEMORY_PROTECT";
    case MH_ERROR_MODULE_NOT_FOUND:
        return "MH_ERROR_MODULE_NOT_FOUND";
    case MH_ERROR_FUNCTION_NOT_FOUND:
        return "MH_ERROR_FUNCTION_NOT_FOUND";
    default:
        return "MH_UNKNOWN";
    }
}

// M2 smoke: install + fire + remove a hook on GetTickCount. Proves the full
// MinHook round-trip (CreateHook → EnableHook → trampoline → Disable → Remove)
// without depending on game state. Replaced by real game-function hooks at M3.
using PFN_GetTickCount = DWORD(WINAPI *)();

PFN_GetTickCount g_real_gettickcount = nullptr;
volatile LONG g_smoke_calls = 0;

DWORD WINAPI smoke_detour()
{
    InterlockedIncrement(&g_smoke_calls);
    return g_real_gettickcount ? g_real_gettickcount() : 0;
}

class MinHookEngine final : public pal::IHookEngine
{
  public:
    pal::HookId install_hook(void *target, void *replacement, void **trampoline) override
    {
        void *original = nullptr;
        const auto s_create = MH_CreateHook(target, replacement, &original);
        if (s_create != MH_OK)
        {
            pal::logf(pal::LogLevel::Error, "MinHookEngine: CreateHook failed at %p (%s)", target, mh_status_name(s_create));
            return pal::kInvalidHookId;
        }
        const auto s_enable = MH_EnableHook(target);
        if (s_enable != MH_OK)
        {
            MH_RemoveHook(target);
            pal::logf(pal::LogLevel::Error, "MinHookEngine: EnableHook failed at %p (%s)", target, mh_status_name(s_enable));
            return pal::kInvalidHookId;
        }
        if (trampoline)
            *trampoline = original;
        const auto id = next_id_.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(mu_);
        targets_[id] = target;
        return id;
    }

    pal::HookId install_listener(void * /*target*/, pal::Listener /*l*/, void * /*user*/) override
    {
        pal::logf(pal::LogLevel::Warn, "MinHookEngine: install_listener not implemented");
        return pal::kInvalidHookId;
    }

    void remove_hook(pal::HookId id) override
    {
        void *target = nullptr;
        {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = targets_.find(id);
            if (it == targets_.end())
                return;
            target = it->second;
            targets_.erase(it);
        }
        MH_DisableHook(target);
        MH_RemoveHook(target);
    }

  private:
    std::atomic<pal::HookId> next_id_{1};
    std::mutex mu_;
    std::unordered_map<pal::HookId, void *> targets_;
};

MinHookEngine &default_engine()
{
    static MinHookEngine e;
    return e;
}

pal::IHookEngine *g_engine_override = nullptr;

void run_minhook_smoke()
{
    g_smoke_calls = 0;
    g_real_gettickcount = nullptr;

    auto *target = reinterpret_cast<void *>(&GetTickCount);
    const auto s_create = MH_CreateHook(target, reinterpret_cast<void *>(&smoke_detour), reinterpret_cast<void **>(&g_real_gettickcount));
    const auto s_enable = MH_EnableHook(target);

    DWORD t = 0;
    if (s_create == MH_OK && s_enable == MH_OK)
        t = GetTickCount();

    const auto s_disable = MH_DisableHook(target);
    const auto s_remove = MH_RemoveHook(target);

    pal::logf(pal::LogLevel::Info, "hook_engine: minhook smoke create=%s enable=%s disable=%s remove=%s calls=%ld t=%lu", mh_status_name(s_create),
              mh_status_name(s_enable), mh_status_name(s_disable), mh_status_name(s_remove), static_cast<long>(g_smoke_calls), static_cast<unsigned long>(t));
}

} // namespace

namespace pal
{

void init_hook_engine()
{
    if (g_initialized)
        return;
    MH_STATUS rc = MH_Initialize();
    if (rc == MH_OK || rc == MH_ERROR_ALREADY_INITIALIZED)
    {
        g_initialized = true;
        logf(LogLevel::Info, "hook_engine: MinHook initialized (%s)", mh_status_name(rc));
        run_minhook_smoke();
    }
    else
    {
        logf(LogLevel::Error, "hook_engine: MH_Initialize failed (%s)", mh_status_name(rc));
    }
}

void shutdown_hook_engine()
{
    if (!g_initialized)
        return;
    MH_STATUS rc = MH_Uninitialize();
    g_initialized = false;
    logf(LogLevel::Info, "hook_engine: MinHook shutdown (%s)", mh_status_name(rc));
}

IHookEngine &hook_engine()
{
    return g_engine_override ? *g_engine_override : default_engine();
}

void set_hook_engine(IHookEngine *e)
{
    g_engine_override = e;
}

} // namespace pal
