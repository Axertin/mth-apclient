#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "MinaModAPI.h"

namespace mth::test
{

// Records InstallHook registrations and serves a canned revision. One active per test; call reset().
struct ModApiRecorder
{
    std::unordered_map<std::string, MM_HookCallback> hooks;
    std::uint32_t revision = 148716; // a plausible real r-number
    bool install_returns_null = false;
    float health = 0.0f; // served by PlayerGetHealth
    int spark = 0;       // served by PlayerGetSpark
    int deaths = 0;      // counts PlayerDie calls

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
        install_returns_null = false;
        health = 0.0f;
        spark = 0;
        deaths = 0;
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

// A MinaModAPI wired to the recorder stubs. reset() the recorder before use.
inline MinaModAPI make_fake_api()
{
    MinaModAPI mm{};
    mm.APIVersion = MinaModAPI_Version;
    mm.InstallHook = &fake_install_hook;
    mm.RemoveHook = &fake_remove_hook;
    mm.GetGameRevision = &fake_get_revision;
    mm.PlayerGetHealth = &fake_get_health;
    mm.PlayerGetSpark = &fake_get_spark;
    mm.PlayerDie = &fake_player_die;
    return mm;
}

} // namespace mth::test
