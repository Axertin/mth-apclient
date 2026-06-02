#include "mth/rando_hooks.hpp"

#include "mth/core/game_symbols.hpp"
#include "mth/core/rando_bridge.hpp"
#include "pal/pal_hook.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

namespace
{

mth::RandoBridge *g_bridge = nullptr;
pal::HookId g_id = pal::kInvalidHookId;

// void Items::OnPickupDone(int slot, int itemType, Player*, ycVec3 const&,
//                          int, int, unsigned int, bool)
void (*g_orig_on_pickup_done)(int, int, void *, void *, int, int, unsigned int, bool) = nullptr;

void repl_on_pickup_done(int slot, int item_type, void *player, void *vec, int a5, int a6, unsigned int a7, bool a8)
{
    if (g_orig_on_pickup_done)
        g_orig_on_pickup_done(slot, item_type, player, vec, a5, a6, a7, a8);
    // Diagnostic: log every observed pickup (read-only MVP). Pickups are
    // infrequent, so per-call logging is fine and confirms the detour fires
    // even before an AP server is connected.
    pal::logf(pal::LogLevel::Info, "OnPickupDone: slot=%d itemType=%d", slot, item_type);
    if (g_bridge)
        g_bridge->on_location_collected(slot);
}

} // namespace

namespace mth
{

RandoHooks::RandoHooks(RandoBridge &bridge)
{
    g_bridge = &bridge;
    const auto addr = pal::resolve_game_symbol(sym::on_pickup_done);
    if (addr == 0)
    {
        pal::logf(pal::LogLevel::Error, "RandoHooks: symbol %s not found; OnPickupDone not hooked", sym::on_pickup_done);
        g_bridge = nullptr;
        return;
    }
    void *target = reinterpret_cast<void *>(addr);
    g_id = pal::hook_engine().install_hook(target, reinterpret_cast<void *>(&repl_on_pickup_done), reinterpret_cast<void **>(&g_orig_on_pickup_done));
    if (g_id == pal::kInvalidHookId)
    {
        pal::logf(pal::LogLevel::Error, "RandoHooks: failed to hook OnPickupDone at %p", target);
        g_bridge = nullptr;
        return;
    }
    installed_ = true;
    pal::logf(pal::LogLevel::Info, "RandoHooks: hooked OnPickupDone at %p via symbol (id=%llu)", target, static_cast<unsigned long long>(g_id));
}

RandoHooks::~RandoHooks()
{
    if (installed_)
        pal::hook_engine().remove_hook(g_id);
    g_bridge = nullptr;
}

} // namespace mth
