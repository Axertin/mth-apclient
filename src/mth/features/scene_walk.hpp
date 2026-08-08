#pragma once

#include <cstddef>
#include <cstdint>

#include "pal/pal_mem.hpp"

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

} // namespace mth
