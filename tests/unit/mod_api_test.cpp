#include <catch2/catch_test_macros.hpp>

#include "mocks/fake_mod_api.hpp"
#include "mod/mod_api.hpp"

namespace
{
int g_forced = -1;
int forced_query(int /*loc*/, bool /*ownership*/)
{
    return g_forced;
}
bool g_world_fired = false;
void world_cb()
{
    g_world_fired = true;
}
} // namespace

TEST_CASE("mod: game_revision reflects the API, 0 when unset", "[mod]")
{
    mth::test::recorder().reset();
    mod::set_api(nullptr);
    REQUIRE(mod::game_revision() == 0);

    auto fake = mth::test::make_fake_api();
    mth::test::recorder().revision = 42;
    mod::set_api(&fake);
    REQUIRE(mod::game_revision() == 42);
    mod::set_api(nullptr);
}

TEST_CASE("mod: IsItemCollected trampoline marshals the query result", "[mod]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);
    REQUIRE(mod::install_item_collected_hook(&forced_query));

    mod::IsItemCollectedCtx ctx{};
    ctx.index = 5;

    g_forced = -1; // pass through: game keeps its own answer
    ctx.mod_handled = false;
    mth::test::recorder().fire("IsItemCollected", &ctx);
    REQUIRE_FALSE(ctx.mod_handled);

    g_forced = 1; // force true
    ctx.mod_handled = false;
    ctx.mod_ret_val = false;
    mth::test::recorder().fire("IsItemCollected", &ctx);
    REQUIRE(ctx.mod_handled);
    REQUIRE(ctx.mod_ret_val);

    g_forced = 0; // force false
    ctx.mod_handled = false;
    ctx.mod_ret_val = true;
    mth::test::recorder().fire("IsItemCollected", &ctx);
    REQUIRE(ctx.mod_handled);
    REQUIRE_FALSE(ctx.mod_ret_val);

    mod::remove_item_collected_hook();
    mod::set_api(nullptr);
}

TEST_CASE("mod: IsItemCollected trampoline ignores negative index", "[mod]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);
    REQUIRE(mod::install_item_collected_hook(&forced_query));

    mod::IsItemCollectedCtx ctx{};
    ctx.index = -1;
    g_forced = 1;
    ctx.mod_handled = false;
    mth::test::recorder().fire("IsItemCollected", &ctx);
    REQUIRE_FALSE(ctx.mod_handled); // index < 0 -> untouched

    mod::remove_item_collected_hook();
    mod::set_api(nullptr);
}

TEST_CASE("mod: WorldUpdate hook fires the registered callback", "[mod]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);
    g_world_fired = false;
    REQUIRE(mod::install_world_update_hook(&world_cb));

    mth::test::recorder().fire("WorldUpdate", nullptr);
    REQUIRE(g_world_fired);

    mod::remove_world_update_hook();
    mod::set_api(nullptr);
}

TEST_CASE("mod: install fails when the API is absent or InstallHook returns null", "[mod]")
{
    mth::test::recorder().reset();
    mod::set_api(nullptr);
    REQUIRE_FALSE(mod::install_item_collected_hook(&forced_query));
    REQUIRE_FALSE(mod::install_world_update_hook(&world_cb));

    auto fake = mth::test::make_fake_api();
    mth::test::recorder().install_returns_null = true;
    mod::set_api(&fake);
    REQUIRE_FALSE(mod::install_item_collected_hook(&forced_query));
    REQUIRE_FALSE(mod::install_world_update_hook(&world_cb));
    mod::set_api(nullptr);
}
