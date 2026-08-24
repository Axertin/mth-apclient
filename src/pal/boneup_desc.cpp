#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include "mod/mod_api.hpp"
#include "mth/core/data/game_layout.hpp"
#include "mth/core/stat_cap_state.hpp"
#include "pal/pal_game.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_mem.hpp"
#include "pal/pal_module.hpp"

namespace
{

pal::BoneupCapFn g_cap_fn;

template <typename T> T read_at(void *base, std::ptrdiff_t off)
{
    return *reinterpret_cast<T *>(static_cast<char *>(base) + off);
}

// A live game object carries a vtable in the module's read-only data, so a drifted offset that lands on
// another live pointer is rejected before anything writes to it. scene_walk.hpp does the same for the
// mth/ walks; pal/ sits below it and cannot include it.
bool looks_like_game_object(const void *p)
{
    if (!pal::pointer_looks_valid(p))
        return false;
    const void *vtable = *static_cast<const void *const *>(p);
    if (!pal::pointer_looks_valid(vtable))
        return false;
    static std::uintptr_t base = 0;
    static std::size_t size = 0;
    if (size == 0)
    {
        const pal::ModuleInfo gm = pal::game_module();
        base = gm.base;
        size = gm.size;
    }
    if (size == 0)
        return true; // no module range published: the pointer checks are all we have
    const auto v = reinterpret_cast<std::uintptr_t>(vtable);
    return v >= base && v < base + size;
}

// Runs at frame rate, so a reading is logged only when it changes. Silent while the menu is closed:
// LevelUpMenu ticks with no display for the whole session otherwise.
void probe(int state, int stat, const void *display, const void *text, const char *current)
{
    static int last = -1;
    const int kind = current == nullptr ? 0 : (*current == '\0' ? 1 : 2);
    const int key = ((state + 1) * 64) + ((stat + 1) * 4) + kind;
    if (key == last)
        return;
    last = key;
    pal::logf(pal::LogLevel::Debug, "boneup probe: state=%d stat=%d display=%p text=%p str=\"%.40s\"", state, stat, display, text,
              current != nullptr ? current : "<null>");
}

} // namespace

namespace pal
{

void set_boneup_cap_provider(BoneupCapFn cap)
{
    g_cap_fn = std::move(cap);
}

void boneup_annotate_description(void *level_up_menu)
{
    if (!g_cap_fn || level_up_menu == nullptr)
        return;

    const int state = read_at<int>(level_up_menu, mth::layout::kLevelUpMenuStateOff);
    const int stat = read_at<int>(level_up_menu, mth::layout::kLevelUpMenuStatOff);

    // Both offsets are pinned on Linux and on Windows r149150, so a mismatch means the game moved. Each
    // hop is checked before use.
    void *display = read_at<void *>(level_up_menu, mth::layout::kLevelUpMenuDisplayOff);
    void *text = looks_like_game_object(display) ? read_at<void *>(display, mth::layout::kLevelUpDisplayDescOff) : nullptr;
    const char *current = looks_like_game_object(text) ? mod::text_of(text) : nullptr;

    if (display != nullptr || state != 0)
        probe(state, stat, display, text, current);

    if (state != mth::layout::kLevelUpMenuInteractiveState)
        return;
    if (current == nullptr || *current == '\0')
        return; // constructed but not yet filled: the menu intro, and the frame a purchase rebuilds it

    const int display_cap = g_cap_fn(stat);
    if (display_cap < 0)
        return;

    // Runs every frame the menu is open, so settle the common already-annotated case without allocating.
    char want[16];
    const int want_len = std::snprintf(want, sizeof want, " (%d)", display_cap);
    const char *eol = std::strchr(current, '\n');
    const std::size_t line1 = eol != nullptr ? static_cast<std::size_t>(eol - current) : std::strlen(current);
    if (want_len > 0 && line1 >= static_cast<std::size_t>(want_len) &&
        std::memcmp(current + line1 - static_cast<std::size_t>(want_len), want, static_cast<std::size_t>(want_len)) == 0)
        return;

    const std::string annotated = mth::boneup_with_cap_suffix(current, display_cap);
    if (annotated != current)
    {
        const bool ok = mod::set_text(text, annotated.c_str());
        pal::logf(pal::LogLevel::Debug, "boneup: stat=%d cap=%d set_text=%d line1=\"%.48s\"", stat, display_cap, ok ? 1 : 0, annotated.c_str());
    }
}

} // namespace pal
