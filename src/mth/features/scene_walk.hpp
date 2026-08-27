#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "mod/mod_api.hpp"
#include "mth/core/data/component_types.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_mem.hpp"
#include "pal/pal_module.hpp"

namespace mth
{

// Runaway guards shared by the scene walks. The graph is a tree, but these bound the damage if a build
// ever hands back a cyclic or corrupt one rather than letting the walk hang the game thread.
inline constexpr std::size_t kSceneMaxNodes = 65536;
inline constexpr std::size_t kSceneMaxChildren = 8192;

// ComponentIsa jumps straight through the object's vtable with no validation of its own, so only pass it
// something shaped like a live polymorphic game object. Note the vtable lives in the module's read-only
// DATA, not its code, so pal::in_game_text is the wrong test here - it would reject every real object.
[[nodiscard]] inline bool looks_like_component(const void *p, std::uintptr_t mod_base, std::size_t mod_size)
{
    if (!pal::pointer_looks_valid(p))
        return false;
    const void *vtable = *static_cast<const void *const *>(p);
    if (!pal::pointer_looks_valid(vtable))
        return false;
    if (mod_size == 0)
        return true; // no module range published (tests): the pointer checks are all we have
    const auto v = reinterpret_cast<std::uintptr_t>(vtable);
    return v >= mod_base && v < mod_base + mod_size;
}

// What one walk did. The caps are reported rather than logged here so each feature keeps its own log
// prefix and issue reference, and so a walk stays usable from a context with no logger.
struct SceneWalk
{
    std::size_t visited{0};
    void *widest_node{nullptr};          // first node that exceeded kSceneMaxChildren, else null
    std::size_t widest_node_children{0}; // its true child count, which is larger than what was walked
    bool node_budget_spent{false};       // stopped at kSceneMaxNodes with work possibly left
    bool stopped_by_visitor{false};      // a visitor returned false, so the walk ended on its own terms
};

// Depth-first walk of the live scene graph from `root`, the general way to find a game object without
// depending on an address that moves every build. Descends through anything the game reports as a
// ycEntity and hands each node's other validated children to `visit` as one span, so a caller can reason
// about siblings rather than one child at a time. `pending` and `buffer` are the caller's scratch, kept
// across ticks so a per-frame walk does not reallocate.
//
// visit is called as visit(children) and returns whether to keep walking, so a search can stop the
// moment it finds its target instead of touring the rest of the room.
template <typename Visit>
SceneWalk walk_scene(void *root, std::uintptr_t mod_base, std::size_t mod_size, std::vector<void *> &pending, std::vector<void *> &buffer, Visit &&visit)
{
    SceneWalk out;
    pending.clear(); // no game pointer outlives the walk that produced it, on any exit path below
    if (root == nullptr)
        return out;

    pending.push_back(root);
    while (!pending.empty() && out.visited < kSceneMaxNodes)
    {
        void *entity = pending.back();
        pending.pop_back();

        const std::size_t count = mod::entity_children(entity, nullptr, 0); // sizing call
        if (count == 0)
            continue;
        // Walk a prefix rather than abandoning the node: skipping it would drop its whole subtree.
        if (count > kSceneMaxChildren && out.widest_node == nullptr)
        {
            out.widest_node = entity;
            out.widest_node_children = count;
        }
        buffer.assign(count > kSceneMaxChildren ? kSceneMaxChildren : count, nullptr);
        mod::entity_children(entity, buffer.data(), buffer.size());

        // Entities are removed from the span before visit sees it, so a visitor never pays for an isa
        // call on a node the walk is already descending into.
        std::size_t kept = 0;
        for (void *c : buffer)
        {
            if (!looks_like_component(c, mod_base, mod_size))
                continue;
            ++out.visited;
            // An entity is a component that holds the next level down, so the graph is walked through it.
            if (mod::component_isa(c, rtti::kYcEntity))
                pending.push_back(c);
            else
                buffer[kept++] = c;
        }
        if (kept != 0 && !visit(std::span<void *const>(buffer.data(), kept)))
        {
            out.stopped_by_visitor = true;
            pending.clear();
            return out;
        }
    }
    out.node_budget_spent = out.visited >= kSceneMaxNodes;
    pending.clear();
    return out;
}

// The game module range, which looks_like_component needs to range-check a vtable. Resolved on first
// use rather than at startup because the lookup takes the loader lock, and retried until it answers so
// a walk that runs before the module is published does not cache a zero for the rest of the session.
// Game-thread only, like every walk.
[[nodiscard]] inline pal::ModuleInfo game_range()
{
    static pal::ModuleInfo gm{};
    if (gm.size == 0)
        gm = pal::game_module();
    return gm;
}

[[nodiscard]] inline bool looks_like_component(const void *p)
{
    const pal::ModuleInfo gm = game_range();
    return looks_like_component(p, gm.base, gm.size);
}

// One scene-polling feature's walking apparatus: the scratch a repeated walk reuses, and the once-only
// latches its diagnostics need. Features hold one instead of carrying six members and their own copy of
// the reporting, which was identical at every site bar a log prefix and a noun.
class SceneWalker
{
  public:
    // tag prefixes every line ("intro"), noun names what is being looked for ("the weapon chest"),
    // suffix carries an issue reference or is empty, and walk_name distinguishes a walk over something
    // other than the game world.
    SceneWalker(const char *tag, const char *noun, const char *suffix = "", const char *walk_name = "scene walk")
        : tag_(tag), noun_(noun), suffix_(suffix), walk_name_(walk_name)
    {
    }

    template <typename Visit> SceneWalk walk(void *root, Visit &&visit)
    {
        const pal::ModuleInfo gm = game_range();
        return walk_scene(root, gm.base, gm.size, pending_, buffer_, std::forward<Visit>(visit));
    }

    // Hand every component of `type` to fn. Walks the whole graph: a room can hold more than one.
    template <typename Fn> SceneWalk for_each(void *root, std::uint64_t type, Fn &&fn)
    {
        return walk(root,
                    [&](std::span<void *const> children)
                    {
                        for (void *c : children)
                            if (mod::component_isa(c, type))
                                fn(c);
                        return true;
                    });
    }

    // Stop at the first component of `type`. SceneWalk::stopped_by_visitor is the "found it" flag.
    template <typename Fn> SceneWalk find_first(void *root, std::uint64_t type, Fn &&fn)
    {
        return walk(root,
                    [&](std::span<void *const> children)
                    {
                        for (void *c : children)
                            if (mod::component_isa(c, type))
                            {
                                fn(c);
                                return false;
                            }
                        return true;
                    });
    }

    // Both runaway guards plus the silence check, each reported once per feature rather than per walk.
    void report(const SceneWalk &w)
    {
        if (w.widest_node != nullptr && !warned_capped_)
        {
            warned_capped_ = true;
            pal::logf(pal::LogLevel::Warn, "%s: scene node %p has %zu children; walking the first %zu%s", tag_, w.widest_node, w.widest_node_children,
                      kSceneMaxChildren, suffix_);
        }
        if (w.node_budget_spent && !warned_capped_)
        {
            warned_capped_ = true;
            pal::logf(pal::LogLevel::Warn, "%s: %s hit the %zu node cap; %s may be past it%s", tag_, walk_name_, kSceneMaxNodes, noun_, suffix_);
        }
        report_silence(w, true);
        if (w.visited != 0 && !logged_extent_)
        {
            logged_extent_ = true;
            pal::logf(pal::LogLevel::Debug, "%s: %s reached %zu components%s", tag_, walk_name_, w.visited, suffix_);
        }
    }

    // The failure mode of every one of these walks is silence: finding nothing looks exactly like a room
    // that has nothing. Reaching zero components off a valid root is the one reading that can only mean
    // broken, so that alone warns, and only when the caller says the answer mattered this tick.
    void report_silence(const SceneWalk &w, bool mattered)
    {
        if (w.visited != 0 || !mattered || warned_empty_)
            return;
        warned_empty_ = true;
        pal::logf(pal::LogLevel::Warn, "%s: %s reached no components; %s cannot be found%s", tag_, walk_name_, noun_, suffix_);
    }

  private:
    const char *tag_;
    const char *noun_;
    const char *suffix_;
    const char *walk_name_;
    bool warned_capped_{false};
    bool warned_empty_{false};
    bool logged_extent_{false};
    std::vector<void *> pending_; // entities left to descend into (explicit stack, no recursion)
    std::vector<void *> buffer_;  // reused across levels and ticks; no per-node allocation
};

} // namespace mth
