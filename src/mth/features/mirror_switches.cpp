#include "mth/features/mirror_switches.hpp"

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

#include "mod/mod_api.hpp"
#include "mth/core/ap/ap_ids.hpp"
#include "mth/core/ap/ap_state.hpp"
#include "mth/core/data/component_types.hpp"
#include "mth/core/data/game_state_ids.hpp"
#include "mth/features/scene_walk.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_mem.hpp"

namespace
{

// SwitchOre fields, carved off its constructor in the Linux build. SwitchOre carries a second vptr at
// +0x178 for its CombatCoreImplementor sub-object, which is exactly the shape where MSVC and Itanium can
// disagree about layout, so these are NOT known to hold on Windows. Run the `switches` probe there and
// check the values look sane before trusting the feature on that platform.
constexpr std::ptrdiff_t kStateOff = 0x194;      // int; 0 = on, 2 = off
constexpr std::ptrdiff_t kWorldOff = 0x168;      // ptr; the SwitchManager hangs off it
constexpr std::ptrdiff_t kTimerOff = 0x200;      // float; < 0 = untimed, > 0 = auto-off after this long
constexpr std::ptrdiff_t kSwitchIdOff = 0x204;   // int; indexes the SwitchManager's state array
constexpr std::ptrdiff_t kColorOff = 0x208;      // uint 0..4
constexpr std::ptrdiff_t kCanTurnOffOff = 0x210; // bool; cleared by the CantTurnOff level property

// SwitchManager, reached as *(switch + kWorldOff) + kManagerOff. Its state array is the live authority for
// whether a switch is on: the switch polls its own entry each tick and transitions to match, and so does
// everything else keyed to the same SwitchID, the colored platforms included. Writing the entry is
// therefore the whole feature, and the switch's own state entry does the vanilla persist on the way.
constexpr std::ptrdiff_t kManagerOff = 0x3a8;
constexpr std::ptrdiff_t kStateArrayOff = 0x470;
constexpr int kMaxSwitchId = 0x40; // the game's own bound on SwitchID, and the length of the array

// Ticks before a failed walk is retried, matching the other scene-walking features. Only the failure paths
// wait: an established list is reused every frame with no walk at all.
constexpr int kRetryCooldownTicks = 60;

// Level-data order, which is what the palette table pins the Color property to.
const char *color_name(std::uint32_t c)
{
    switch (c)
    {
    case 0:
        return "yellow";
    case 1:
        return "green";
    case 2:
        return "blue";
    case 3:
        return "purple";
    case 4:
        return "red";
    default:
        return "?";
    }
}

// One switch this feature drives. The component is held through a weak pointer, the game's own answer to
// outliving the thing you found: it reports the object's death rather than leaving a raw pointer that has
// to be guessed about. The id and color are copied so a reused slot can be told from the original.
struct Managed
{
    void *weak;
    int id;
    std::uint32_t color;
};

std::atomic<bool> g_probe_requested{false};
std::atomic<bool> g_console_override{false};
std::uint32_t g_logged_granted{0xffffffffu}; // impossible mask, so the first pass always logs
bool g_logged_inert{false};
std::vector<Managed> g_managed;
void *g_managed_world{nullptr}; // the world g_managed was walked from; null means the list is cold
int g_retry_cooldown{0};
bool g_warned_api{false};
mth::SceneWalker g_walker{"switches", "a rainbow switch", " (#28)"};

void forget()
{
    for (const Managed &m : g_managed)
        mod::weak_ptr_destroy(m.weak);
    g_managed.clear();
    g_managed_world = nullptr;
}

int switch_id(const void *component)
{
    return *reinterpret_cast<const int *>(static_cast<const char *>(component) + kSwitchIdOff);
}

std::uint32_t switch_color(const void *component)
{
    return *reinterpret_cast<const std::uint32_t *>(static_cast<const char *>(component) + kColorOff);
}

// The switch's entry in its world's SwitchManager, or null when the chain does not look walkable.
int *manager_entry(const void *component, int id)
{
    if (id < 0 || id >= kMaxSwitchId)
        return nullptr;
    const char *sw = static_cast<const char *>(component);
    void *world = *reinterpret_cast<void *const *>(sw + kWorldOff);
    // Vtable range check rather than a bare pointer test: this hop is the one that would otherwise carry a
    // plausible-looking value into a raw offset read if the object layout ever drifted.
    if (!mth::looks_like_component(world))
        return nullptr;
    void *mgr = *reinterpret_cast<void *const *>(static_cast<const char *>(world) + kManagerOff);
    if (!pal::pointer_looks_valid(mgr)) // SwitchManager is not known to be polymorphic, so no vtable test
        return nullptr;
    return reinterpret_cast<int *>(static_cast<char *>(mgr) + kStateArrayOff + id * 4);
}

// The shortcut switches are the permanent, untimed ones. A CantTurnOff switch that still carries a timer is
// a legal authoring combination the game has palettes for, and driving one would fight its own auto-off.
//
// This is the whole discriminator, and it is an assumption: only the four switches of one mirror-hub room
// have been seen, so a permanent untimed rainbow switch elsewhere in the hub would be driven too. Every
// switch the feature takes charge of is logged for that reason.
bool is_shortcut_switch(const void *component)
{
    const char *sw = static_cast<const char *>(component);
    return *reinterpret_cast<const std::uint8_t *>(sw + kCanTurnOffOff) == 0 && *reinterpret_cast<const float *>(sw + kTimerOff) < 0.0f;
}

void dump_switch(void *component, int n)
{
    const char *sw = static_cast<const char *>(component);
    const int id = switch_id(component);
    const std::uint32_t color = switch_color(component);
    const int *entry = manager_entry(component, id);
    pal::logf(pal::LogLevel::Info, "switches: [%d] %p id=%d color=%u(%s) state=%d live=%d canTurnOff=%d timer=%.1f", n, component, id, color, color_name(color),
              *reinterpret_cast<const int *>(sw + kStateOff), entry != nullptr ? *entry : -1,
              *reinterpret_cast<const std::uint8_t *>(sw + kCanTurnOffOff) != 0 ? 1 : 0, static_cast<double>(*reinterpret_cast<const float *>(sw + kTimerOff)));
}

// Drives every managed switch to the granted set. False means the list no longer describes the room, which
// is the caller's signal to drop it and walk again rather than write through the rest of it.
bool apply(std::uint32_t granted)
{
    for (const Managed &m : g_managed)
    {
        void *component = mod::weak_ptr_get(m.weak);
        if (component == nullptr)
            return false; // the switch died with its room
        int *entry = manager_entry(component, m.id);
        if (entry == nullptr)
            return false;
        const int want = ((granted >> m.color) & 1u) != 0 ? 1 : 0;
        if (*entry == want)
            continue;
        // Only a real transition logs, so this marks the grant, or the moment a switch the player hit by
        // hand went back to shut, rather than repeating every frame.
        *entry = want;
        pal::logf(pal::LogLevel::Info, "switches: %s switch (id %d) forced %s (#28)", color_name(m.color), m.id, want != 0 ? "open" : "shut");
    }
    return true;
}

// The set as a log line. Silence about an empty mask is what makes a switch that never opens look like a
// broken clamp rather than an item that never arrived.
std::string granted_names(std::uint32_t mask)
{
    if (mask == 0)
        return "nothing";
    std::string out;
    for (std::uint32_t c = 0; c < static_cast<std::uint32_t>(mth::kAstralPlatformCount); ++c)
        if (((mask >> c) & 1u) != 0)
            out += (out.empty() ? "" : ", ") + std::string(color_name(c));
    return out;
}

std::uint32_t granted_colors(const mth::ApState &state)
{
    std::uint32_t mask = 0;
    for (const auto &it : state.received_items())
        if (mth::is_astral_platform_item(it.item_id))
            mask |= 1u << static_cast<unsigned>(mth::astral_platform_color(it.item_id));
    return mask;
}

// Takes charge of one switch found by the walk. Everything it refuses is reported, because the failure mode
// of the whole feature is a switch quietly not being driven.
void manage(void *component)
{
    const int id = switch_id(component);
    const std::uint32_t color = switch_color(component);
    // Bounds the shift in apply(), and a permanent switch reporting anything else is layout drift.
    if (color >= static_cast<std::uint32_t>(mth::kAstralPlatformCount))
    {
        pal::logf(pal::LogLevel::Warn, "switches: permanent switch id %d reports color %u, out of range; left vanilla (#28)", id, color);
        return;
    }
    for (const Managed &m : g_managed)
        if (m.color == color)
        {
            // One AP item would drive both, which is a level-data reading this feature cannot honor.
            pal::logf(pal::LogLevel::Warn, "switches: a second %s switch (id %d) shares this room; left vanilla (#28)", color_name(color), id);
            return;
        }
    void *weak = mod::weak_ptr_create(component);
    if (weak == nullptr)
    {
        pal::logf(pal::LogLevel::Warn, "switches: no weak pointer for the %s switch (id %d); left vanilla (#28)", color_name(color), id);
        return;
    }
    g_managed.push_back({weak, id, color});
    // Logged here rather than over the finished list, so it says what was taken charge of exactly when that
    // happened. The discriminator is an assumption and one of the five has never been seen, so a room that
    // reports nothing is how either would surface without anyone running the probe.
    pal::logf(pal::LogLevel::Info, "switches: driving the %s switch (id %d) from AP (#28)", color_name(color), id);
}

} // namespace

namespace mth
{

void request_switch_probe()
{
    g_probe_requested.store(true);
}

void set_mirror_switches_override_flag(bool on)
{
    g_console_override.store(on);
}

void mirror_switches_on_world_destroy()
{
    forget(); // every room is its own World, and a switch does not outlive one
}

void tick_mirror_switches(const ApState &state, bool slot_ok)
{
    const bool probe = g_probe_requested.exchange(false);
    // Confined to the one area the shortcut switches live in, so everywhere else costs a comparison. The
    // clamp drives a state entry the switch persists, which makes it a durable write, so it waits for the
    // bound AP save the way every other durable write in the mod does.
    // The console override replaces the session gate rather than adding to it, which is the only way to
    // exercise this offline: the seed flag it normally waits on cannot be set by hand. It cannot weaken
    // anything, since it only ever turns enforcement on. The gamestate test is not part of the policy and
    // stays either way.
    const bool console = g_console_override.load();
    const bool armed = console || (state.authenticated() && slot_ok && state.mirror_switch_rando());
    const bool in_hub = mod::current_game_state() == kGameStateMirrorHub && mod::room_index() == kGameStateMirrorHubSwitchRoom;
    const bool wanted = armed && in_hub;
    // Standing among the switches with nothing driving them is indistinguishable from a broken clamp, and
    // the console override does not survive a restart, so say which condition is missing. Once per visit.
    if (!in_hub)
        g_logged_inert = false;
    else if (!armed && !g_logged_inert)
    {
        g_logged_inert = true;
        pal::logf(pal::LogLevel::Debug, "switches: not driving these switches (authed=%d bound_save=%d seed_flag=%d console=%d) (#28)",
                  state.authenticated() ? 1 : 0, slot_ok ? 1 : 0, state.mirror_switch_rando() ? 1 : 0, console ? 1 : 0);
    }
    // The probe only reads, so it needs the walk alone; driving the switches also needs somewhere to keep
    // them. Reported once, because otherwise the feature is simply absent on a build whose modding API is
    // inert and a seed that asked for the switches would never say why they stayed shut.
    const bool walk_api = mod::entity_walk_api_available();
    const bool enforce = wanted && walk_api && mod::weak_ptr_api_available();
    if (wanted && !enforce && !g_warned_api)
    {
        g_warned_api = true;
        pal::logf(pal::LogLevel::Warn, "switches: modding API unavailable (revision=%u); the shortcut switches are left vanilla (#28)", mod::game_revision());
    }

    if (!enforce)
        forget();
    if (!probe && !enforce)
        return;
    if (!walk_api)
    {
        if (probe) // an explicit console action always gets an answer, latch or no latch
            pal::logf(pal::LogLevel::Warn, "switches: the modding entity-walk API is unavailable (revision=%u)", mod::game_revision());
        return;
    }
    void *world = mod::player_world(); // null until a player is live, which is also when no room exists
    if (world == nullptr)
    {
        if (probe)
            pal::logf(pal::LogLevel::Warn, "switches: no live world to walk; stand in a room and try again");
        return;
    }
    if (world != g_managed_world)
        forget();

    bool walk_now = probe;
    if (enforce && g_managed_world == nullptr)
    {
        if (g_retry_cooldown > 0)
            --g_retry_cooldown;
        else
            walk_now = true;
    }

    if (walk_now)
    {
        void *root = mod::world_game_root_entity(world);
        if (root == nullptr)
        {
            if (probe)
                pal::logf(pal::LogLevel::Warn, "switches: the world has no scene root to walk");
            return;
        }
        int found = 0;
        const SceneWalk walk = g_walker.for_each(root, rtti::kSwitchOre,
                                                 [&](void *c)
                                                 {
                                                     if (probe)
                                                         dump_switch(c, found++);
                                                     if (enforce && g_managed_world == nullptr && is_shortcut_switch(c))
                                                         manage(c);
                                                 });
        g_walker.report(walk);
        // Latched only on a room that actually held some, so a walk landing before the room finished
        // building is retried rather than remembered as empty.
        if (enforce && g_managed_world == nullptr)
        {
            if (g_managed.empty())
                g_retry_cooldown = kRetryCooldownTicks;
            else
                g_managed_world = world;
        }
        if (probe)
        {
            const auto screen = (static_cast<std::uint32_t>(mod::current_game_state()) << 16) | (static_cast<std::uint32_t>(mod::room_index()) & 0xffffu);
            pal::logf(pal::LogLevel::Info, "switches: %d found in screen %#x", found, screen);
        }
    }

    if (!enforce)
        return;
    const std::uint32_t granted = granted_colors(state);
    if (granted != g_logged_granted)
    {
        g_logged_granted = granted;
        pal::logf(pal::LogLevel::Info, "switches: AP has granted %s (#28)", granted_names(granted).c_str());
    }
    if (g_managed_world != nullptr && !apply(granted))
    {
        forget(); // the list stopped describing the room
        g_retry_cooldown = kRetryCooldownTicks;
    }
}

} // namespace mth
