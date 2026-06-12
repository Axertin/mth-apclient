#pragma once

#include "pal/pal_hook.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

namespace mth
{

// RAII detour on a symbol-resolved game function. A missing symbol or failed install
// logs and leaves the hook inert (installed() == false); destruction removes the hook.
// Replacement/trampoline stay file-scope in the owning module (Frida replacements have
// no user context); this type owns only the id bookkeeping.
class ScopedHook
{
  public:
    ScopedHook() = default;

    ScopedHook(const char *symbol, void *replacement, void **trampoline, const char *label)
    {
        const auto addr = pal::resolve_game_symbol(symbol);
        if (addr == 0)
        {
            pal::logf(pal::LogLevel::Warn, "hook: symbol %s (%s) not found; not hooked", symbol, label);
            return;
        }
        void *target = reinterpret_cast<void *>(addr);
        id_ = pal::hook_engine().install_hook(target, replacement, trampoline);
        if (id_ == pal::kInvalidHookId)
            pal::logf(pal::LogLevel::Error, "hook: failed to hook %s at %p", label, target);
        else
            pal::logf(pal::LogLevel::Info, "hook: hooked %s at %p (id=%llu)", label, target, static_cast<unsigned long long>(id_));
    }

    ~ScopedHook()
    {
        reset();
    }

    ScopedHook(ScopedHook &&other) noexcept : id_(other.id_)
    {
        other.id_ = pal::kInvalidHookId;
    }

    ScopedHook &operator=(ScopedHook &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            id_ = other.id_;
            other.id_ = pal::kInvalidHookId;
        }
        return *this;
    }

    ScopedHook(const ScopedHook &) = delete;
    ScopedHook &operator=(const ScopedHook &) = delete;

    [[nodiscard]] bool installed() const
    {
        return id_ != pal::kInvalidHookId;
    }

    void reset()
    {
        if (id_ != pal::kInvalidHookId)
            pal::hook_engine().remove_hook(id_);
        id_ = pal::kInvalidHookId;
    }

  private:
    pal::HookId id_{pal::kInvalidHookId};
};

} // namespace mth
