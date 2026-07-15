#include <atomic>

#include "pal/pal_game.hpp"

namespace pal
{
namespace
{
std::atomic<bool> g_ap_save_gate{false};
}

void set_ap_save_gate(bool open)
{
    g_ap_save_gate.store(open, std::memory_order_relaxed);
}

bool ap_save_gate()
{
    return g_ap_save_gate.load(std::memory_order_relaxed);
}
} // namespace pal
