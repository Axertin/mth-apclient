#pragma once

// Public hook-engine interface. Frida-gum types are NOT exposed here —
// the default implementation is in src/pal/hook_manager.cpp; tests
// substitute a MockHookEngine via set_hook_engine().

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

// Default engine implementation is platform-specific:
//   Linux   -> Frida-gum backed (GumInterceptor)
//   Windows -> MinHook backed
// The default is initialized lazily on first use; tests inject MockHookEngine
// via set_hook_engine() before any pal::hook_engine() call.
IHookEngine &hook_engine();
void set_hook_engine(IHookEngine *);

// Explicitly bring the default engine up / tear it down. Called from
// pal::apclient_main() to validate linkage and prepare for use. Platform
// adapters implement these in src/pal/{linux,windows}/hook_engine_*.cpp.
void init_hook_engine();
void shutdown_hook_engine();

} // namespace pal
