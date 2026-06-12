#include "mth/core/config.hpp"

#include <cstdio>
#include <cstdlib>

#include "pal/pal_log.hpp"

namespace mth
{

Config parse_config(const EnvGetter &getter)
{
    Config cfg;

    if (const char *locks = getter("MTHAP_REMOVE_LOCKS"); locks && *locks)
        cfg.remove_locks_csv = locks;

    if (const char *m = getter("MTHAP_MODIFIERS"); m && *m)
    {
        cfg.modifiers = parse_modifier_indices(m);
        cfg.modifiers_from_env = true;
    }

    if (const char *sc = getter("MTHAP_STAT_CAPS"); sc && *sc)
    {
        int a = 0, b = 0, c = 0;
        if (std::sscanf(sc, "%d,%d,%d", &a, &b, &c) == 3)
            cfg.stat_caps = {a, b, c};
        else
            pal::logf(pal::LogLevel::Warn, "config: MTHAP_STAT_CAPS=%s malformed (want a,b,c); ignored", sc);
    }

    if (const char *mock = getter("MTHAP_MOCK_AP"); mock && *mock)
    {
        int max_idx = std::atoi(mock);
        if (max_idx < 2)
            max_idx = 1024;
        cfg.mock_ap_max_idx = max_idx;
    }

    if (const char *dl = getter("MTHAP_DEATHLINK"); dl && *dl && std::atoi(dl) != 0)
        cfg.deathlink = true;

    if (const char *server = getter("MTHAP_AP_SERVER"); server && *server)
        cfg.ap_server = server;
    if (const char *slot = getter("MTHAP_AP_SLOT"); slot && *slot)
        cfg.ap_slot = slot;
    if (const char *password = getter("MTHAP_AP_PASSWORD"); password && *password)
        cfg.ap_password = password;

    return cfg;
}

Config load_config_from_env()
{
    return parse_config([](const char *name) { return std::getenv(name); });
}

} // namespace mth
