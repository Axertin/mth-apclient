#include "mth/features/ticket_machine_gate.hpp"

#include <cstddef>
#include <cstdint>

#include "mod/mod_api.hpp"
#include "mth/core/data/component_types.hpp"
#include "mth/core/data/game_layout.hpp"
#include "mth/features/scene_walk.hpp"
#include "pal/pal_log.hpp"

namespace
{

// Ticks between walks. A full traversal is microseconds, but it is pure waste in every room that has no
// machine, so this runs at about 1Hz rather than per frame.
constexpr int kWalkIntervalTicks = 60;

using mth::looks_like_component;

// Disables the machine's interact component. Writes only on a real transition, so the log marks the one
// tick that changed something instead of repeating at the walk cadence - and would speak up again if the
// game ever cleared the byte, which is worth knowing.
void disable_machine(void *machine)
{
    void *ic = *reinterpret_cast<void **>(static_cast<char *>(machine) + mth::layout::kTicketMachineInteractOff);
    if (!looks_like_component(ic) || !mod::component_isa(ic, mth::rtti::kInteractComponent))
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

    const SceneWalk walk = walker_.for_each(root, rtti::kTicketMachine, [&](void *c) { disable_machine(c); });
    walker_.report(walk);
}

void TicketMachineGate::on_world_destroy()
{
    cooldown_ = 0;
}

} // namespace mth
