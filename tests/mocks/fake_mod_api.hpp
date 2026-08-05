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
