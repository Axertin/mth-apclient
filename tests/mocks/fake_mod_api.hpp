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

// A MinaModAPI wired to the recorder stubs. reset() the recorder before use.
inline MinaModAPI make_fake_api()
{
    MinaModAPI mm{};
    mm.APIVersion = MinaModAPI_Version;
    mm.InstallHook = &fake_install_hook;
    mm.RemoveHook = &fake_remove_hook;
    mm.GetGameRevision = &fake_get_revision;
    return mm;
}

} // namespace mth::test
