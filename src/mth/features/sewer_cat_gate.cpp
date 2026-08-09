#include "mth/features/sewer_cat_gate.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

#include "mod/mod_api.hpp"
#include "mth/core/data/component_types.hpp"
#include "mth/core/data/game_layout.hpp"
#include "mth/features/scene_walk.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

namespace
{

// Ticks between walks. Matches the donation-machine gate: a full traversal is microseconds, but it is
// pure waste in every room he is not in, so this runs at about 1Hz rather than per frame.
constexpr int kWalkIntervalTicks = 60;

constexpr std::size_t kMaxNodes = mth::kSceneMaxNodes;
constexpr std::size_t kMaxChildren = mth::kSceneMaxChildren;
using mth::looks_like_component;

// The game's own ownership link, and the only unambiguous answer: it names THIS vendor's component.
// Both hops are validated rather than trusted, since the offsets were read off the Linux build.
[[nodiscard]] void *interact_via_owner(void *behavior, std::uintptr_t mod_base, std::size_t mod_size)
{
    void *owner = *reinterpret_cast<void **>(static_cast<char *>(behavior) + mth::layout::kSewerCatEntityOff);
    if (!looks_like_component(owner, mod_base, mod_size))
        return nullptr;
    void *ic = *reinterpret_cast<void **>(static_cast<char *>(owner) + mth::layout::kNpcEntityInteractOff);
    if (!looks_like_component(ic, mod_base, mod_size) || !mod::component_isa(ic, mth::rtti::kInteractComponent))
        return nullptr;
    return ic;
}

} // namespace

namespace mth
{

SewerCatGate::SewerCatGate(std::function<bool()> should_disable) : should_disable_(std::move(should_disable))
{
}

// Writes only on a real transition, so the log marks the one tick that changed something instead of
// repeating at the walk cadence - and would speak up again if the game ever cleared the byte.
//
// The owner link is authoritative and is what decides. The sibling the walk already validated is only a
// fallback for a build whose owner offset has moved, and only when the child list held exactly one
// interact component: nothing structurally binds "an interact component under this entity" to THIS
// vendor, so with two NPCs on one entity an unconditional sibling would disable the wrong one - and then
// hide it forever, since every later walk re-finds it already set. Which path won is logged because the
// fallback firing means an offset needs re-checking.
void SewerCatGate::disable_vendor(void *behavior, void *sibling_interact, bool sibling_unique)
{
    const char *via = "owner";
    void *ic = interact_via_owner(behavior, mod_base_, mod_size_);
    if (ic == nullptr && sibling_unique)
    {
        ic = sibling_interact;
        via = "sibling";
    }
    if (ic == nullptr)
    {
        if (!warned_no_interact_)
        {
            warned_no_interact_ = true;
            pal::logf(pal::LogLevel::Warn, "panino: fetch vendor %p has no unambiguous interact component; left live (#88)", behavior);
        }
        return;
    }
    auto *disabled = reinterpret_cast<std::uint8_t *>(static_cast<char *>(ic) + mth::layout::kInteractDisabledOff);
    if (*disabled != 0)
        return;
    *disabled = 1;
    pal::logf(pal::LogLevel::Info, "panino: fetch vendor %p disabled (interact %p via %s) (#88)", behavior, ic, via);
}

void SewerCatGate::tick()
{
    if (!should_disable_ || !should_disable_())
        return;
    if (!mod::entity_walk_api_available())
        return;
    if (cooldown_ > 0)
    {
        --cooldown_;
        return;
    }
    void *world = mod::player_world(); // null until a player is live, which is also when no room exists
    if (world == nullptr)
        return;
    void *root = mod::world_game_root_entity(world);
    if (root == nullptr)
        return;
    // Only once a walk is actually going to run: charging the cooldown above would let the first tick
    // after a room load - the one most likely to have no live player yet - eat the on_world_destroy
    // reset and leave him interactible for a full interval, in the room the player just walked into.
    cooldown_ = kWalkIntervalTicks;

    if (mod_size_ == 0)
    {
        const pal::ModuleInfo gm = pal::game_module();
        mod_base_ = gm.base;
        mod_size_ = gm.size;
    }
    std::size_t visited = 0;

    pending_.clear();
    pending_.push_back(root);
    while (!pending_.empty() && visited < kMaxNodes)
    {
        void *entity = pending_.back();
        pending_.pop_back();

        const std::size_t count = mod::entity_children(entity, nullptr, 0); // sizing call
        if (count == 0)
            continue;
        // Walk a prefix rather than abandoning the node: skipping it would drop its whole subtree.
        buffer_.assign(count > kMaxChildren ? kMaxChildren : count, nullptr);
        if (count > kMaxChildren && !warned_capped_)
        {
            warned_capped_ = true;
            pal::logf(pal::LogLevel::Warn, "panino: scene node %p has %zu children; walking the first %zu (#88)", entity, count, kMaxChildren);
        }
        mod::entity_children(entity, buffer_.data(), buffer_.size());

        // Behaviour and interact component are docked as siblings, so both come out of the same pass.
        // The count is what makes the sibling usable: one interact component under this entity means it
        // can only be his, several mean it cannot be attributed and disable_vendor must not guess.
        void *vendor = nullptr;
        void *interact = nullptr;
        std::size_t interact_count = 0;
        for (void *c : buffer_)
        {
            if (!looks_like_component(c, mod_base_, mod_size_))
                continue;
            ++visited;
            // An entity is a component that holds the next level down, so the graph is walked through it.
            if (mod::component_isa(c, rtti::kYcEntity))
                pending_.push_back(c);
            else if (mod::component_isa(c, rtti::kNpcBehaviorSewerCat))
                vendor = c;
            else if (mod::component_isa(c, rtti::kInteractComponent))
            {
                interact = c;
                ++interact_count;
            }
        }
        if (vendor != nullptr)
        {
            disable_vendor(vendor, interact, interact_count == 1);
            pending_.clear(); // no game pointer outlives the walk that produced it
            return;           // one dedicated instance per world; nothing left to find
        }
    }

    if (visited >= kMaxNodes && !warned_capped_)
    {
        warned_capped_ = true;
        pal::logf(pal::LogLevel::Warn, "panino: scene walk hit the %zu node cap; the vendor may be past it (#88)", kMaxNodes);
    }
    // The failure mode of this walk is silence: a broken traversal finds no vendor, which is also what
    // every room without him looks like. Reaching zero components off a valid root is the one reading
    // that can only mean broken, so it warns; the extent of a working walk is logged once for scale.
    if (visited == 0)
    {
        if (!warned_empty_)
        {
            warned_empty_ = true;
            pal::logf(pal::LogLevel::Warn, "panino: scene walk reached no components; the fetch vendor cannot be found (#88)");
        }
        return;
    }
    if (!logged_extent_)
    {
        logged_extent_ = true;
        pal::logf(pal::LogLevel::Debug, "panino: scene walk reached %zu components (#88)", visited);
    }
}

void SewerCatGate::on_world_destroy()
{
    cooldown_ = 0;
}

} // namespace mth
