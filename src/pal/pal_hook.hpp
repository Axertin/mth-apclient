#pragma once

#include <cstdint>

namespace pal
{

using HookId = std::uint64_t;
constexpr HookId kInvalidHookId = 0;

struct Listener
{
    void (*on_enter)(void *ctx, void *user);
    void (*on_leave)(void *ctx, void *user);
};

class IHookEngine
{
  public:
    virtual ~IHookEngine() = default;

    virtual HookId install_hook(void *target, void *replacement, void **trampoline) = 0;

    virtual HookId install_listener(void *target, Listener listener, void *user) = 0;

    virtual void remove_hook(HookId) = 0;
};

IHookEngine &hook_engine();
void set_hook_engine(IHookEngine *);

void init_hook_engine();
void shutdown_hook_engine();

} // namespace pal
