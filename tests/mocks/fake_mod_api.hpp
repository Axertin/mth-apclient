#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "MinaModAPI.h"
#include "mth/core/data/game_layout.hpp"

namespace mth::test
{

// Save-slot state backing the fake save API.
struct FakeSaveState
{
    int active_slot{0};
    std::string contents;
    bool write_enabled{true};
    int restore_calls{0};
    int staged_slot{-1}; // last slot written without activating it
    std::string staged_contents;
};

inline FakeSaveState &fake_save_state()
{
    static FakeSaveState s;
    return s;
}

// Records InstallHook registrations and serves a canned revision. One active per test; call reset().
struct ModApiRecorder
{
    std::unordered_map<std::string, MM_HookCallback> hooks;
    std::uint32_t revision = 148716; // a plausible real r-number
    int game_state = 0;              // served by GetCurrentGameState
    bool install_returns_null = false;
    float health = 0.0f;        // served by PlayerGetHealth
    int spark = 0;              // served by PlayerGetSpark
    int deaths = 0;             // counts PlayerDie calls
    bool paused = false;        // served by WorldIsPaused: a menu that pauses the world
    bool game_paused = false;   // the whole world update queue is skipped; invisible to WorldIsPaused
    bool world_has_area = true; // false models a World with no area bound: menus, the title world, the ending
    float room_time = 0.0f;
    void *player = nullptr;            // served by PlayerGetComponent; the game nulls its global in Player::~Player
    std::uint64_t bosses_defeated = 0; // served by PlayerGetBossesDefeated
    float pos[3]{};                    // served by PlayerGetPos3
    int collected_calls = 0;           // ItemsSetItemCollected
    int collected_index = -1;
    bool collected_value = false;
    int text_color_calls = 0; // TextComponentSetColor
    void *text_color_target = nullptr;
    std::uint8_t text_color[4]{}; // r,g,b,a as the API delivered them
    int text_set_calls = 0;       // TextComponentSetText
    void *text_set_target = nullptr;
    std::string text_value;      // the widget's live string, served back by TextComponentGetText
    int clone_palette_calls = 0; // ClonePalette
    void *clone_palette_source = nullptr;
    void *clone_palette_result = nullptr; // the clone the fake reports; null models allocation failure
    std::int32_t palette_set_group_value = 0;
    void *palette_write_index_target = nullptr;
    std::int32_t palette_write_index_index = 0;
    std::uint8_t palette_write_index_color[4]{};
    std::int32_t palette_get_index_result = 0;
    std::uint32_t palette_get_width_result = 0;
    int water_calls = 0; // WaterListenerIsInDeepWater
    void *water_target = nullptr;
    bool water_ignore_enabled = false;
    bool in_deep_water = false; // the answer the fake serves
    int aabb_calls = 0;         // PhysicsComponentGetAABB
    void *aabb_target = nullptr;
    bool aabb_local = false;
    std::uint32_t aabb_shape_flags = 0;
    float aabb[6]{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f}; // center.xyz then half-extent.xyz, as the API writes it
    int closest_calls = 0;                             // CarryManagerGetClosestCarryableObject
    void *closest_target = nullptr;
    float closest_box[6]{};
    int closest_layer = 0;
    float closest_max_dist = 0.0f;
    std::uint64_t closest_mask = 0;
    void *closest_result = nullptr;               // the carryable the fake reports, null for "nothing in range"
    int update_stats_calls = 0;                   // PlayerUpdateStats
    int cheat_manager_is_cheat_applied_calls = 0; // CheatManagerIsCheatApplied
    bool cheat_manager_is_cheat_applied_result = false;
    int cheat_manager_set_cheat_applied_calls = 0; // CheatManagerSetCheatApplied
    int cheat_manager_set_cheat_index = -1;
    bool cheat_manager_set_cheat_active = false;

    // WorldGetEntityList: the entries the fake serves per list name.
    std::unordered_map<std::string, std::vector<void *>> entity_lists;
    int entity_list_calls = 0;

    // CreateWeakPtr/WeakPtrGet/DestroyWeakPtr. A handle is an index into `weak_targets`; a null entry
    // means the game freed the target, which is the case the chest registry has to survive.
    std::vector<void *> weak_targets;
    int weak_destroys = 0;

    void fire(const char *name, void *ctx)
    {
        auto it = hooks.find(name);
        if (it != hooks.end() && it->second != nullptr)
            it->second(ctx);
    }
    // Assign from a default instance rather than listing members, so a new recorder field cannot leak
    // state into the next test by being forgotten here. The save state lives outside the recorder, so
    // it needs its own line.
    void reset()
    {
        *this = ModApiRecorder{};
        fake_save_state() = FakeSaveState{};
    }
};

inline ModApiRecorder &recorder()
{
    static ModApiRecorder r;
    return r;
}

inline void *fake_install_hook(const char *name, std::int32_t /*priority*/, MM_HookCallback cb)
{
    if (recorder().install_returns_null)
        return nullptr;
    recorder().hooks[name] = cb;
    return reinterpret_cast<void *>(recorder().hooks.size()); // non-null opaque handle
}
inline void fake_remove_hook(void * /*handle*/)
{
}
inline int fake_queue_destroys = 0;
inline void fake_queue_destroy_entity(World * /*w*/, ycEntity * /*e*/, const bool /*depth_first*/)
{
    ++fake_queue_destroys;
}

inline void fake_set_item_collected(std::int32_t index, bool collected, ItemCollection * /*collection*/, SaveSlot * /*slot*/)
{
    ++recorder().collected_calls;
    recorder().collected_index = index;
    recorder().collected_value = collected;
}

// Records the channels as the API delivered them, so a test can pin the packed-word -> MM_Color mapping.
inline void fake_text_set_color(ycComponent *component, MM_Color color)
{
    ++recorder().text_color_calls;
    recorder().text_color_target = component;
    recorder().text_color[0] = color.r;
    recorder().text_color[1] = color.g;
    recorder().text_color[2] = color.b;
    recorder().text_color[3] = color.a;
}

// SetText stores the widget's string and GetText serves it back, so a test can round-trip one widget.
inline void fake_text_set_text(ycComponent *component, const char *const text)
{
    ++recorder().text_set_calls;
    recorder().text_set_target = component;
    recorder().text_value = (text != nullptr) ? text : "";
}
inline const char *fake_text_get_text(ycComponent * /*component*/)
{
    return recorder().text_value.c_str();
}

inline ycPaletteTexture *fake_clone_palette(ycPaletteTexture *pal)
{
    ++recorder().clone_palette_calls;
    recorder().clone_palette_source = pal;
    return static_cast<ycPaletteTexture *>(recorder().clone_palette_result);
}
inline void fake_palette_set_group(ycPaletteTexture * /*pal*/, std::int32_t group)
{
    recorder().palette_set_group_value = group;
}
inline void fake_palette_write_index(ycPaletteTexture *pal, std::int32_t index, MM_Color color)
{
    recorder().palette_write_index_target = pal;
    recorder().palette_write_index_index = index;
    recorder().palette_write_index_color[0] = color.r;
    recorder().palette_write_index_color[1] = color.g;
    recorder().palette_write_index_color[2] = color.b;
    recorder().palette_write_index_color[3] = color.a;
}
inline std::int32_t fake_palette_get_index(ycPaletteTexture * /*pal*/, MM_Color /*color*/)
{
    return recorder().palette_get_index_result;
}
inline std::uint32_t fake_palette_get_width(ycPaletteTexture * /*pal*/)
{
    return recorder().palette_get_width_result;
}

inline bool fake_water_is_in_deep_water(WaterListener *listener, bool ignore_enabled)
{
    ++recorder().water_calls;
    recorder().water_target = listener;
    recorder().water_ignore_enabled = ignore_enabled;
    return recorder().in_deep_water;
}

// Serves the six recorded floats as one MM_AABB, so a test can pin the center.xyz/extents.xyz packing.
inline void fake_physics_get_aabb(PhysicsComponent *component, MM_AABB *out, bool local, std::uint32_t shape_flags)
{
    ++recorder().aabb_calls;
    recorder().aabb_target = component;
    recorder().aabb_local = local;
    recorder().aabb_shape_flags = shape_flags;
    if (out == nullptr)
        return;
    out->center.x = recorder().aabb[0];
    out->center.y = recorder().aabb[1];
    out->center.z = recorder().aabb[2];
    out->extents.x = recorder().aabb[3];
    out->extents.y = recorder().aabb[4];
    out->extents.z = recorder().aabb[5];
}

// Records the by-value box the same way, so the round trip out of physics_get_aabb can be checked end to end.
inline CarryableObject *fake_closest_carryable(CarryManager *manager, MM_AABB box, std::int32_t layer, float max_dist, std::int32_t *out_count,
                                               std::uint64_t mask_ignore)
{
    ++recorder().closest_calls;
    recorder().closest_target = manager;
    recorder().closest_box[0] = box.center.x;
    recorder().closest_box[1] = box.center.y;
    recorder().closest_box[2] = box.center.z;
    recorder().closest_box[3] = box.extents.x;
    recorder().closest_box[4] = box.extents.y;
    recorder().closest_box[5] = box.extents.z;
    recorder().closest_layer = layer;
    recorder().closest_max_dist = max_dist;
    recorder().closest_mask = mask_ignore;
    if (out_count != nullptr)
        *out_count = recorder().closest_result != nullptr ? 1 : 0;
    return static_cast<CarryableObject *>(recorder().closest_result);
}

inline void fake_player_update_stats()
{
    ++recorder().update_stats_calls;
}

inline bool fake_cheat_manager_is_cheat_applied(std::int32_t /*cheat*/, std::uint32_t /*save_slot*/)
{
    ++recorder().cheat_manager_is_cheat_applied_calls;
    return recorder().cheat_manager_is_cheat_applied_result;
}

inline void fake_cheat_manager_set_cheat_applied(std::int32_t cheat, bool active, std::uint32_t /*save_slot*/)
{
    ++recorder().cheat_manager_set_cheat_applied_calls;
    recorder().cheat_manager_set_cheat_index = cheat;
    recorder().cheat_manager_set_cheat_active = active;
}

// Mirrors the real contract: the total count comes back even when the buffer is too small, and a
// null/0 buffer is the sizing call.
inline std::size_t fake_world_entity_list(World * /*world*/, const char *list, GameComponent **out, std::size_t cap)
{
    ++recorder().entity_list_calls;
    auto it = recorder().entity_lists.find((list != nullptr) ? list : "");
    if (it == recorder().entity_lists.end())
        return 0;
    const std::vector<void *> &src = it->second;
    for (std::size_t i = 0; i < src.size() && i < cap; ++i)
        out[i] = static_cast<GameComponent *>(src[i]);
    return src.size();
}

// A handle is (index + 1) so it is never null; get() serves whatever the slot currently holds, which a
// test clears to model the game freeing the target.
inline MM_WeakPtr *fake_create_weak_ptr(void *target)
{
    recorder().weak_targets.push_back(target);
    return reinterpret_cast<MM_WeakPtr *>(recorder().weak_targets.size());
}
inline void *fake_weak_ptr_get(MM_WeakPtr *weak)
{
    const std::size_t idx = reinterpret_cast<std::uintptr_t>(weak);
    if (idx == 0 || idx > recorder().weak_targets.size())
        return nullptr;
    return recorder().weak_targets[idx - 1];
}
inline void fake_destroy_weak_ptr(MM_WeakPtr * /*weak*/)
{
    ++recorder().weak_destroys;
}

inline void *fake_get_sym_addr(const char *name)
{
    // Only the two data symbols, enough to prove the mapping reaches the API.
    if (std::strcmp(name, "s_rItems") == 0)
        return reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x1000));
    return nullptr;
}

inline std::uint32_t fake_get_revision()
{
    return recorder().revision;
}
inline std::int32_t fake_get_game_state()
{
    return recorder().game_state;
}
inline float fake_get_health()
{
    return recorder().health;
}
inline std::int32_t fake_get_spark()
{
    return recorder().spark;
}
// The real PlayerDie does not take effect instantly: health/the guard byte keep reading alive for many polls
// afterwards, so this only records the request. Tests drive the delayed death themselves.
inline void fake_player_die()
{
    ++recorder().deaths;
}
// The room clock runs off the world's gameplay queues: advance it per read, but not while either pause holds
// it. A caller polling once per tick then sees what a real menu or game-level pause looks like.
inline float fake_get_room_time()
{
    if (!recorder().paused && !recorder().game_paused)
        recorder().room_time += 1.0f / 120.0f;
    return recorder().room_time;
}
inline bool fake_world_is_paused(World * /*world*/)
{
    return recorder().paused;
}
// Sized to carry the AreaManager slot Player::InitDeath walks, so a test can serve a populated World that
// has no area bound. The area pointer only has to be non-null; nothing dereferences it.
inline unsigned char *fake_world_storage()
{
    static std::vector<unsigned char> world(static_cast<std::size_t>(mth::layout::kWorldAreaManagerOff) + sizeof(void *), 0);
    return world.data();
}
inline World *fake_player_get_world()
{
    unsigned char *world = fake_world_storage();
    void *area = recorder().world_has_area ? static_cast<void *>(&recorder()) : nullptr;
    std::memcpy(world + mth::layout::kWorldAreaManagerOff, &area, sizeof(area));
    return reinterpret_cast<World *>(world);
}
inline ycComponent *fake_player_get_component()
{
    return static_cast<ycComponent *>(recorder().player);
}
inline std::uint64_t fake_get_bosses_defeated()
{
    return recorder().bosses_defeated;
}
inline MM_Vec3 fake_get_pos3()
{
    MM_Vec3 v{};
    v.x = recorder().pos[0];
    v.y = recorder().pos[1];
    v.z = recorder().pos[2];
    return v;
}

// Fake scene graph for the walk primitive. A node IS its own handle: the walk only ever reads the first
// word (looking for something vtable-shaped) and then asks the API about it, so a plain struct whose
// first member is any valid pointer satisfies every check walk_scene makes.
struct FakeComponent
{
    void *vtable{nullptr};
    std::uint64_t type{0};
    std::uint64_t base{0}; // a second id the node also answers to, since the engine's ComponentIsa is a
                           // hierarchy test and one object can be both an entity and something else
    std::vector<void *> children;
};

struct FakeScene
{
    std::deque<FakeComponent> nodes; // deque: node addresses are the handles, so they must be stable
    int vtable_anchor{0};

    void reset()
    {
        nodes.clear();
    }

    void *add(std::uint64_t type, void *parent, std::uint64_t base = 0)
    {
        FakeComponent &n = nodes.emplace_back();
        n.vtable = &vtable_anchor;
        n.type = type;
        n.base = base;
        if (parent != nullptr)
            static_cast<FakeComponent *>(parent)->children.push_back(&n);
        return &n;
    }
};

inline FakeScene &fake_scene()
{
    static FakeScene s;
    return s;
}

// Must stay non-null: mod::entity_walk_api_available() gates the two entries the walk actually uses on
// this one being present. Tests pass the root they built straight to walk_scene, so it is never called.
inline ycEntity *fake_world_game_root(World *)
{
    return nullptr;
}

inline size_t fake_entity_children(ycEntity *entity, ycComponent **out, size_t cap)
{
    auto *n = reinterpret_cast<FakeComponent *>(entity);
    if (n == nullptr)
        return 0;
    const size_t total = n->children.size();
    if (out != nullptr)
        for (size_t i = 0; i < total && i < cap; ++i)
            out[i] = reinterpret_cast<ycComponent *>(n->children[i]);
    return total; // the real entry reports the true count even when the buffer is short
}

inline bool fake_component_isa(ycComponent *component, MM_Rtti rtti)
{
    auto *n = reinterpret_cast<FakeComponent *>(component);
    return n != nullptr && (n->type == rtti.typeId || (n->base != 0 && n->base == rtti.typeId));
}

// A MinaModAPI wired to the recorder stubs. reset() the recorder before use.
inline MinaModAPI make_fake_api()
{
    MinaModAPI mm{};
    mm.APIVersion = MinaModAPI_Version;
    mm.InstallHook = &fake_install_hook;
    mm.RemoveHook = &fake_remove_hook;
    mm.GetGameRevision = &fake_get_revision;
    mm.WorldQueueDestroyEntity = &fake_queue_destroy_entity;
    mm.ItemsSetItemCollected = &fake_set_item_collected;
    mm.TextComponentSetColor = &fake_text_set_color;
    mm.TextComponentSetText = &fake_text_set_text;
    mm.TextComponentGetText = &fake_text_get_text;
    mm.ClonePalette = &fake_clone_palette;
    mm.PaletteSetGroup = &fake_palette_set_group;
    mm.PaletteWriteIndex = &fake_palette_write_index;
    mm.PaletteGetIndex = &fake_palette_get_index;
    mm.PaletteGetWidth = &fake_palette_get_width;
    mm.WaterListenerIsInDeepWater = &fake_water_is_in_deep_water;
    mm.PhysicsComponentGetAABB = &fake_physics_get_aabb;
    mm.CarryManagerGetClosestCarryableObject = &fake_closest_carryable;
    mm.PlayerUpdateStats = &fake_player_update_stats;
    mm.CheatManagerIsCheatApplied = &fake_cheat_manager_is_cheat_applied;
    mm.CheatManagerSetCheatApplied = &fake_cheat_manager_set_cheat_applied;
    mm.WorldGetEntityList = &fake_world_entity_list;
    mm.WorldGetGameRootEntity = &fake_world_game_root;
    mm.EntityGetChildren = &fake_entity_children;
    mm.ComponentIsa = &fake_component_isa;
    mm.CreateWeakPtr = &fake_create_weak_ptr;
    mm.WeakPtrGet = &fake_weak_ptr_get;
    mm.DestroyWeakPtr = &fake_destroy_weak_ptr;
    mm.GetSymAddr = &fake_get_sym_addr;
    mm.GetCurrentGameState = &fake_get_game_state;
    mm.PlayerGetHealth = &fake_get_health;
    mm.PlayerGetSpark = &fake_get_spark;
    mm.PlayerDie = &fake_player_die;
    mm.GetRoomTime = &fake_get_room_time;
    mm.WorldIsPaused = &fake_world_is_paused;
    mm.PlayerGetWorld = &fake_player_get_world;
    mm.PlayerGetComponent = &fake_player_get_component;
    mm.PlayerGetBossesDefeated = &fake_get_bosses_defeated;
    mm.PlayerGetPos3 = &fake_get_pos3;
    mm.GetActiveSaveSlot = [] { return fake_save_state().active_slot; };
    mm.SetActiveSaveSlot = [](std::uint32_t slot) { fake_save_state().active_slot = static_cast<int>(slot); };
    mm.SetActiveSaveSlotContents = [](const char *d) -> bool
    {
        if (d == nullptr)
            return false;
        fake_save_state().contents = d;
        return true;
    };
    mm.SetSaveSlotContents = [](std::uint32_t slot, const char *d) -> bool
    {
        if (d == nullptr)
            return false;
        fake_save_state().staged_slot = static_cast<int>(slot);
        fake_save_state().staged_contents = d;
        return true;
    };
    mm.GetActiveSaveSlotContents = []() -> char *
    {
        const std::string &s = fake_save_state().contents;
        char *buf = static_cast<char *>(std::malloc(s.size() + 1));
        std::memcpy(buf, s.c_str(), s.size() + 1);
        return buf;
    };
    mm.Free = [](void *p) { std::free(p); };
    mm.StartActiveSaveSlot = [] {};
    mm.PlayerRestoreFromSave = [] { ++fake_save_state().restore_calls; };
    mm.SetSaveWriteEnabled = [](bool on) { fake_save_state().write_enabled = on; };
    mm.IsSaveWriteEnabled = [] { return fake_save_state().write_enabled; };
    return mm;
}

} // namespace mth::test
