#include <map>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/config.hpp"

namespace
{

mth::EnvGetter env_of(std::map<std::string, std::string> vars)
{
    return [vars = std::move(vars)](const char *name) -> const char *
    {
        const auto it = vars.find(name);
        return it == vars.end() ? nullptr : it->second.c_str();
    };
}

} // namespace

TEST_CASE("empty environment yields inert defaults", "[config]")
{
    const mth::Config cfg = mth::parse_config(env_of({}));
    REQUIRE(cfg.remove_locks_csv.empty());
    REQUIRE(cfg.modifiers.indices.empty());
    REQUIRE_FALSE(cfg.modifiers_from_env);
    REQUIRE_FALSE(cfg.stat_caps.has_value());
    REQUIRE_FALSE(cfg.mock_ap_max_idx.has_value());
    REQUIRE_FALSE(cfg.deathlink);
    REQUIRE(cfg.ap_server.empty());
    REQUIRE(cfg.ap_slot == "Player1");
    REQUIRE(cfg.ap_password.empty());
}

TEST_CASE("stat caps parse a,b,c and reject malformed", "[config]")
{
    const auto ok = mth::parse_config(env_of({{"MTHAP_STAT_CAPS", "1,2,3"}}));
    REQUIRE(ok.stat_caps.has_value());
    REQUIRE((*ok.stat_caps)[0] == 1);
    REQUIRE((*ok.stat_caps)[1] == 2);
    REQUIRE((*ok.stat_caps)[2] == 3);

    const auto bad = mth::parse_config(env_of({{"MTHAP_STAT_CAPS", "1,2"}}));
    REQUIRE_FALSE(bad.stat_caps.has_value());
}

TEST_CASE("mock AP index clamps small values to the 1024 default", "[config]")
{
    REQUIRE(*mth::parse_config(env_of({{"MTHAP_MOCK_AP", "1"}})).mock_ap_max_idx == 1024);
    REQUIRE(*mth::parse_config(env_of({{"MTHAP_MOCK_AP", "junk"}})).mock_ap_max_idx == 1024);
    REQUIRE(*mth::parse_config(env_of({{"MTHAP_MOCK_AP", "50"}})).mock_ap_max_idx == 50);
}

TEST_CASE("deathlink requires a nonzero value", "[config]")
{
    REQUIRE(mth::parse_config(env_of({{"MTHAP_DEATHLINK", "1"}})).deathlink);
    REQUIRE_FALSE(mth::parse_config(env_of({{"MTHAP_DEATHLINK", "0"}})).deathlink);
    REQUIRE_FALSE(mth::parse_config(env_of({})).deathlink);
}

TEST_CASE("modifiers list parses and sets the env flag", "[config]")
{
    const auto cfg = mth::parse_config(env_of({{"MTHAP_MODIFIERS", "3, force:19"}}));
    REQUIRE(cfg.modifiers_from_env);
    REQUIRE(cfg.modifiers.indices.size() == 2);
    REQUIRE(cfg.modifiers.forced.count(19) == 1);
}

TEST_CASE("AP connection settings pass through with slot default", "[config]")
{
    const auto cfg = mth::parse_config(env_of({{"MTHAP_AP_SERVER", "host:38281"}, {"MTHAP_AP_PASSWORD", "pw"}}));
    REQUIRE(cfg.ap_server == "host:38281");
    REQUIRE(cfg.ap_slot == "Player1");
    REQUIRE(cfg.ap_password == "pw");
    REQUIRE(mth::parse_config(env_of({{"MTHAP_AP_SERVER", "h"}, {"MTHAP_AP_SLOT", "MySlot"}})).ap_slot == "MySlot");
}
