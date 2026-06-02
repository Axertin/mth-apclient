#include <atomic>
#include <mutex>
#include <unordered_map>

#include <frida-gum.h>

#include "pal/pal_hook.hpp"
#include "pal/pal_log.hpp"

namespace
{

bool g_initialized = false;

class FridaHookEngine final : public pal::IHookEngine
{
  public:
    pal::HookId install_hook(void *target, void *replacement, void **trampoline) override
    {
        GumInterceptor *ic = gum_interceptor_obtain();
        gpointer original = nullptr;
        const auto rc = gum_interceptor_replace(ic, target, replacement, nullptr, &original);
        if (rc != GUM_REPLACE_OK)
        {
            pal::logf(pal::LogLevel::Error, "FridaHookEngine: replace failed at %p (rc=%d)", target, rc);
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
        // Listener-mode (enter/leave) requires a GumInvocationListener; deferred
        // until a consumer needs it.
        pal::logf(pal::LogLevel::Warn, "FridaHookEngine: install_listener not yet implemented");
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
        gum_interceptor_revert(gum_interceptor_obtain(), target);
    }

  private:
    std::atomic<pal::HookId> next_id_{1};
    std::mutex mu_;
    std::unordered_map<pal::HookId, void *> targets_;
};

FridaHookEngine &default_engine()
{
    static FridaHookEngine e;
    return e;
}

pal::IHookEngine *g_engine_override = nullptr;

struct EnumState
{
    int count{0};
    int sample_logged{0};
};

gboolean on_module(const GumModuleDetails *m, gpointer ud)
{
    auto *st = static_cast<EnumState *>(ud);
    ++st->count;
    if (st->sample_logged < 5 && m && m->name)
    {
        pal::logf(pal::LogLevel::Debug, "hook_engine: module[%d] name=%s", st->count - 1, m->name);
        ++st->sample_logged;
    }
    return TRUE; // keep iterating
}

} // namespace

namespace pal
{

void init_hook_engine()
{
    if (g_initialized)
        return;
    gum_init_embedded();
    g_initialized = true;
    logf(LogLevel::Info, "hook_engine: frida-gum initialized");

    EnumState state{};
    gum_process_enumerate_modules(&on_module, &state);
    const auto *main_mod = gum_process_get_main_module();
    logf(LogLevel::Info, "hook_engine: %d modules loaded, main=%s", state.count, (main_mod && main_mod->name) ? main_mod->name : "?");
}

void shutdown_hook_engine()
{
    if (!g_initialized)
        return;
    gum_deinit_embedded();
    g_initialized = false;
    logf(LogLevel::Info, "hook_engine: frida-gum shutdown");
}

IHookEngine &hook_engine()
{
    return g_engine_override ? *g_engine_override : default_engine();
}

void set_hook_engine(IHookEngine *e)
{
    g_engine_override = e;
}

// FridaHookEngine is the default; tests can override it via set_hook_engine().
// No production code installs hooks yet - the first real game hook lands in a
// later milestone.

} // namespace pal
