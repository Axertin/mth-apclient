#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

#include "MinaModAPI.h"

namespace mth::test
{

// Save-slot state backing the fake save API.
struct FakeSaveState
{
    int active_slot{0};
    std::string contents;
    bool write_enabled{true};
    int start_calls{0};
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
    float health = 0.0f;      // served by PlayerGetHealth
    int spark = 0;            // served by PlayerGetSpark
    int deaths = 0;           // counts PlayerDie calls
    bool paused = false;      // served by WorldIsPaused: a menu that pauses the world
    bool game_paused = false; // the whole world update queue is skipped; invisible to WorldIsPaused
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
    std::string text_value; // the widget's live string, served back by TextComponentGetText
    int water_calls = 0;    // WaterListenerIsInDeepWater
    void *water_target = nullptr;
    bool water_ignore_enabled = false;
    bool in_deep_water = false; // the answer the fake serves

    void fire(const char *name, void *ctx)
    {
        auto it = hooks.find(name);
        if (it != hooks.end() && it->second != nullptr)
            it->second(ctx);
    }
    void reset()
    {
        hooks.clear();
        revision = 148716;
        game_state = 0;
        install_returns_null = false;
        health = 0.0f;
        spark = 0;
        deaths = 0;
        paused = false;
        game_paused = false;
        room_time = 0.0f;
        player = nullptr;
        bosses_defeated = 0;
        pos[0] = pos[1] = pos[2] = 0.0f;
        collected_calls = 0;
        collected_index = -1;
        collected_value = false;
        text_color_calls = 0;
        text_color_target = nullptr;
        text_color[0] = text_color[1] = text_color[2] = text_color[3] = 0;
        text_set_calls = 0;
        text_set_target = nullptr;
        text_value.clear();
        water_calls = 0;
        water_target = nullptr;
        water_ignore_enabled = false;
        in_deep_water = false;
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

inline bool fake_water_is_in_deep_water(WaterListener *listener, bool ignore_enabled)
{
    ++recorder().water_calls;
    recorder().water_target = listener;
    recorder().water_ignore_enabled = ignore_enabled;
    return recorder().in_deep_water;
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
inline World *fake_player_get_world()
{
    static int world; // opaque non-null handle; the fake pause getter ignores it
    return reinterpret_cast<World *>(&world);
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
    mm.WaterListenerIsInDeepWater = &fake_water_is_in_deep_water;
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
    mm.StartActiveSaveSlot = [] { ++fake_save_state().start_calls; };
    mm.PlayerRestoreFromSave = [] { ++fake_save_state().restore_calls; };
    mm.SetSaveWriteEnabled = [](bool on) { fake_save_state().write_enabled = on; };
    mm.IsSaveWriteEnabled = [] { return fake_save_state().write_enabled; };
    return mm;
}

} // namespace mth::test
