#include "mth/features/ticket_machine_gate.hpp"

#include <cstddef>
#include <cstdint>

#include "mod/mod_api.hpp"
#include "mth/core/data/component_types.hpp"
#include "mth/core/data/game_layout.hpp"
#include "mth/features/scene_walk.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_mem.hpp"
#include "pal/pal_module.hpp"

namespace
{

// Ticks between walks. A full traversal is microseconds, but it is pure waste in every room that has no
// machine, so this runs at about 1Hz rather than per frame.
constexpr int kWalkIntervalTicks = 60;

using mth::looks_like_component;

// Disables the machine's interact component. Writes only on a real transition, so the log marks the one
// tick that changed something instead of repeating at the walk cadence - and would speak up again if the
// game ever cleared the byte, which is worth knowing.
void disable_machine(void *machine, std::uintptr_t mod_base, std::size_t mod_size)
{
    void *ic = *reinterpret_cast<void **>(static_cast<char *>(machine) + mth::layout::kTicketMachineInteractOff);
    if (!looks_like_component(ic, mod_base, mod_size) || !mod::component_isa(ic, mth::rtti::kInteractComponent))
    {
        pal::logf(pal::LogLevel::Warn, "train: donation machine %p has no interact component at +%#zx; left live (#162)", machine,
                  static_cast<std::size_t>(mth::layout::kTicketMachineInteractOff));
        return;
    }
    auto *disabled = reinterpret_cast<std::uint8_t *>(static_cast<char *>(ic) + mth::layout::kInteractDisabledOff);
    if (*disabled != 0)
        return;
    *disabled = 1;
    pal::logf(pal::LogLevel::Info, "train: donation machine %p disabled (interact %p) (#162)", machine, ic);
}

} // namespace

namespace mth
{

void TicketMachineGate::tick()
{
    if (!mod::entity_walk_api_available())
        return;
    if (cooldown_ > 0)
    {
        --cooldown_;
        return;
    }
    cooldown_ = kWalkIntervalTicks;

    void *world = mod::player_world(); // null until a player is live, which is also when no room exists
    if (world == nullptr)
        return;
    void *root = mod::world_game_root_entity(world);
    if (root == nullptr)
        return;

    if (mod_size_ == 0)
    {
        const pal::ModuleInfo gm = pal::game_module();
        mod_base_ = gm.base;
        mod_size_ = gm.size;
    }

    const SceneWalk walk = walk_scene(root, mod_base_, mod_size_, pending_, buffer_,
                                      [&](void *, std::span<void *const> children)
                                      {
                                          for (void *c : children)
                                              if (mod::component_isa(c, rtti::kTicketMachine))
                                                  disable_machine(c, mod_base_, mod_size_);
                                          return true;
                                      });
    const std::size_t visited = walk.visited;
    if (walk.widest_node != nullptr && !warned_capped_)
    {
        warned_capped_ = true;
        // A tile-heavy room having a wide node says nothing about whether the machine is under it.
        pal::logf(pal::LogLevel::Warn, "train: scene node %p has %zu children; walking the first %zu (#162)", walk.widest_node, walk.widest_node_children,
                  kSceneMaxChildren);
    }
    if (walk.node_budget_spent && !warned_capped_)
    {
        warned_capped_ = true;
        pal::logf(pal::LogLevel::Warn, "train: scene walk hit the %zu node cap; the machine may be past it (#162)", kSceneMaxNodes);
    }

    // The failure mode of this walk is silence: a broken traversal finds no machine, which is also exactly
    // what every room without one looks like. Reaching zero components off a valid root is the one reading
    // that can only mean broken, so it warns; the extent of a working walk is logged once for scale.
    if (visited == 0)
    {
        if (!warned_empty_)
        {
            warned_empty_ = true;
            pal::logf(pal::LogLevel::Warn, "train: scene walk reached no components; the donation machine cannot be found (#162)");
        }
        return;
    }
    if (!logged_extent_)
    {
        logged_extent_ = true;
        pal::logf(pal::LogLevel::Debug, "train: scene walk reached %zu components (#162)", visited);
    }
}

void TicketMachineGate::on_world_destroy()
{
    cooldown_ = 0;
}

} // namespace mth
