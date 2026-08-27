#include "mth/features/intro_chest_gate.hpp"

#include <cstddef>
#include <cstdint>

#include "mod/mod_api.hpp"
#include "mth/core/data/component_types.hpp"
#include "mth/core/data/game_layout.hpp"
#include "mth/features/scene_walk.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

namespace
{

// About 1Hz, matching the other scene walks: a traversal costs microseconds but is waste in every room
// with no chest in it.
constexpr int kWalkIntervalTicks = 60;

constexpr std::size_t kMaxNodes = mth::kSceneMaxNodes;
using mth::looks_like_component;

} // namespace

namespace mth
{

void IntroChestGate::set_armed(bool on)
{
    armed_ = on;
}

// Writes only on a real transition, so a demotion costs one log line rather than one per walk, and a re-arm
// by the NPC is reported again. The menu is cleared as well as the chest: the NPC writes the menu's copy of
// the byte directly, so clearing the chest alone leaves an already-open menu in starter mode.
void IntroChestGate::demote_chest(void *chest)
{
    auto *starter = reinterpret_cast<std::uint8_t *>(static_cast<char *>(chest) + mth::layout::kCheckpointChestStarterOff);
    void *menu = *reinterpret_cast<void **>(static_cast<char *>(chest) + mth::layout::kCheckpointChestMenuOff);
    std::uint8_t *menu_starter = nullptr;
    if (looks_like_component(menu, mod_base_, mod_size_) && mod::component_isa(menu, mth::rtti::kWeaponsChestMenu))
        menu_starter = reinterpret_cast<std::uint8_t *>(static_cast<char *>(menu) + mth::layout::kWeaponsChestMenuStarterOff);

    if (*starter == 0 && (menu_starter == nullptr || *menu_starter == 0))
        return; // the common case is a pure read: every chest outside the intro is already in this mode

    const unsigned prev_chest = *starter;
    const unsigned prev_menu = menu_starter != nullptr ? *menu_starter : 0u;
    *starter = 0;
    if (menu_starter != nullptr)
        *menu_starter = 0;
    const pal::LogLevel level = logged_demoted_ ? pal::LogLevel::Debug : pal::LogLevel::Info;
    logged_demoted_ = true;
    pal::logf(level, "intro: weapon chest %p demoted to weapon-change mode (chest starter %u -> 0, menu %p starter %u -> 0)", chest, prev_chest, menu,
              prev_menu);
}

void IntroChestGate::tick()
{
    if (!armed_)
        return;
    if (!mod::entity_walk_api_available())
        return;
    if (cooldown_ > 0)
    {
        --cooldown_;
        return;
    }
    void *world = mod::player_world(); // null until a player is live
    if (world == nullptr)
        return;
    void *root = mod::world_game_root_entity(world);
    if (root == nullptr)
        return;
    // Charged only when a walk runs, so the first tick after a room load (which usually has no live player
    // yet) cannot eat the on_world_destroy reset and cost a full interval.
    cooldown_ = kWalkIntervalTicks;

    if (mod_size_ == 0)
    {
        const pal::ModuleInfo gm = pal::game_module();
        mod_base_ = gm.base;
        mod_size_ = gm.size;
    }

    const SceneWalk walk = walk_scene(root, mod_base_, mod_size_, pending_, buffer_,
                                      [&](void *, std::span<void *const> children)
                                      {
                                          // No early exit: a checkpoint room carries a trinket chest and a weapon chest, and
                                          // only the one still in starter mode is written, so the rest cost a read each.
                                          for (void *c : children)
                                              if (mod::component_isa(c, rtti::kCheckpointChest))
                                                  demote_chest(c);
                                          return true;
                                      });
    const std::size_t visited = walk.visited;
    if (walk.widest_node != nullptr && !warned_capped_)
    {
        warned_capped_ = true;
        pal::logf(pal::LogLevel::Warn, "intro: scene node %p has %zu children; walking the first %zu", walk.widest_node, walk.widest_node_children,
                  kSceneMaxChildren);
    }
    if (walk.node_budget_spent && !warned_capped_)
    {
        warned_capped_ = true;
        pal::logf(pal::LogLevel::Warn, "intro: scene walk hit the %zu node cap; the weapon chest may be past it", kSceneMaxNodes);
    }

    // The failure mode of this walk is silence: a broken traversal finds no chest, which is also what every
    // room without one looks like. Reaching zero components off a valid root is the one reading that can
    // only mean broken, so it warns; the extent of a working walk is logged once for scale.
    if (visited == 0)
    {
        if (!warned_empty_)
        {
            warned_empty_ = true;
            pal::logf(pal::LogLevel::Warn, "intro: scene walk reached no components; the weapon chest cannot be found");
        }
        return;
    }
    if (!logged_extent_)
    {
        logged_extent_ = true;
        pal::logf(pal::LogLevel::Debug, "intro: scene walk covered %zu components (node cap %zu)", visited, kMaxNodes);
    }
}

void IntroChestGate::on_world_destroy()
{
    cooldown_ = 0;
}

} // namespace mth
