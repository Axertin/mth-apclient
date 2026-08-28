#include "mth/features/status_menu_caps.hpp"

#include <string>

#include "mod/mod_api.hpp"
#include "mth/core/ap/ap_ids.hpp" // kStatCount
#include "mth/core/data/component_types.hpp"
#include "mth/core/data/game_layout.hpp"
#include "mth/core/stat_cap_state.hpp"
#include "mth/features/levelcap_hooks.hpp"
#include "mth/features/scene_walk.hpp"
#include "pal/pal_log.hpp"

namespace mth
{
namespace
{
// The labels are rewritten only on menu build and language change, so a few frames of latency after
// the pause screen opens go unnoticed. Walking every tick would pay a full traversal for nothing.
constexpr int kWalkEveryTicks = 10;
} // namespace

StatusMenuCaps::StatusMenuCaps(const LevelCapHooks &caps) : caps_(caps)
{
}

void StatusMenuCaps::on_world_destroy()
{
    logged_found_ = false;
}

void StatusMenuCaps::on_world_update_end(void *world)
{
    if (world == nullptr)
        return;
    // A player who never connects would otherwise pay a traversal several times a second all session.
    if (caps_.display_cap_for(0) < 0)
        return;
    if (++ticks_ < kWalkEveryTicks)
        return;
    ticks_ = 0;

    if (!mod::entity_walk_api_available() || !mod::world_menu_root_api_available())
        return;
    void *root = mod::world_menu_root_entity(world);
    if (root == nullptr)
        return;

    // StatusMenu is a plain ycComponent, so it is a leaf: the walk never descends into it.
    const SceneWalk walk = walker_.find_first(root, rtti::kStatusMenu, [&](void *c) { annotate(c); });
    // Absence is the normal case (the pause screen is open for seconds at a time), so only a walk that
    // reached nothing at all is worth reporting.
    walker_.report_silence(walk, true);
}

void StatusMenuCaps::annotate(void *status_menu)
{
    if (!logged_found_)
    {
        logged_found_ = true;
        pal::logf(pal::LogLevel::Debug, "statuscaps: StatusMenu found at %p", status_menu);
    }

    for (int stat = 0; stat < kStatCount; ++stat)
    {
        const int display_cap = caps_.display_cap_for(stat);
        if (display_cap < 0)
            continue; // not enforcing: leave the vanilla label alone

        void *widget = *reinterpret_cast<void **>(static_cast<char *>(status_menu) + mth::layout::status_lvl_widget_offset(stat));
        // A drifted offset can land on another live object, and this path writes to what it finds, so
        // the vtable is checked against the module the way the scene walks check theirs.
        if (!looks_like_component(widget))
        {
            pal::logf(pal::LogLevel::Debug, "statuscaps: stat=%d level widget rejected (%p)", stat, widget);
            continue;
        }

        const char *current = mod::text_of(widget);
        if (current == nullptr || *current == '\0')
            continue; // built but not yet filled

        const std::string annotated = status_panel_with_cap_suffix(current, display_cap);
        if (annotated == current)
            continue;

        const bool ok = mod::set_text(widget, annotated.c_str());
        pal::logf(pal::LogLevel::Debug, "statuscaps: stat=%d cap=%d set_text=%d \"%.32s\"", stat, display_cap, ok ? 1 : 0, annotated.c_str());
    }
}

} // namespace mth
