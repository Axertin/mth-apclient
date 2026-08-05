#include <catch2/catch_test_macros.hpp>

#include "mocks/fake_mod_api.hpp"
#include "mod/mod_api.hpp"
#include "pal/pal_module.hpp"

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
bool g_destroy_fired = false;
void destroy_cb()
{
    g_destroy_fired = true;
}

// Restores the process-wide range on scope exit so a failing REQUIRE mid-test cannot leave it
// stuck at a value that would make every later test's usable() reject the fake api's pointers.
struct TextRangeGuard
{
    pal::TextRange saved{pal::text_range_storage()};

    ~TextRangeGuard()
    {
        pal::set_game_text_range(saved);
    }
};
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

TEST_CASE("mod: current_game_state reflects the API, -1 when unset", "[mod]")
{
    mth::test::recorder().reset();
    mod::set_api(nullptr);
    REQUIRE(mod::current_game_state() == -1);

    auto fake = mth::test::make_fake_api();
    mth::test::recorder().game_state = 7;
    mod::set_api(&fake);
    REQUIRE(mod::current_game_state() == 7);
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

TEST_CASE("mod: WorldDestroy hook fires the registered callback", "[mod]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);
    g_destroy_fired = false;
    REQUIRE(mod::install_world_destroy_hook(&destroy_cb));

    mth::test::recorder().fire("WorldDestroy", nullptr);
    REQUIRE(g_destroy_fired);

    mod::remove_world_destroy_hook();
    mod::set_api(nullptr);
}

TEST_CASE("mod: install fails when the API is absent or InstallHook returns null", "[mod]")
{
    mth::test::recorder().reset();
    mod::set_api(nullptr);
    REQUIRE_FALSE(mod::install_item_collected_hook(&forced_query));
    REQUIRE_FALSE(mod::install_world_update_hook(&world_cb));
    REQUIRE_FALSE(mod::install_world_destroy_hook(&destroy_cb));

    auto fake = mth::test::make_fake_api();
    mth::test::recorder().install_returns_null = true;
    mod::set_api(&fake);
    REQUIRE_FALSE(mod::install_item_collected_hook(&forced_query));
    REQUIRE_FALSE(mod::install_world_update_hook(&world_cb));
    REQUIRE_FALSE(mod::install_world_destroy_hook(&destroy_cb));
    mod::set_api(nullptr);
}

TEST_CASE("save api reports unavailable without an api pointer", "[mod][save]")
{
    mod::set_api(nullptr);
    REQUIRE_FALSE(mod::save_api_available());
    REQUIRE(mod::active_save_slot() == -1);
    REQUIRE(mod::active_save_slot_contents().empty());
}

TEST_CASE("save api passthroughs reach the fake api", "[mod][save]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);

    REQUIRE(mod::save_api_available());

    mth::test::fake_save_state().active_slot = 3;
    REQUIRE(mod::active_save_slot() == 3);

    REQUIRE(mod::set_active_save_slot_contents("[YCD Version: 1]\nSaveSlot\n{}"));
    REQUIRE(mod::active_save_slot_contents() == "[YCD Version: 1]\nSaveSlot\n{}");
    REQUIRE_FALSE(mod::set_active_save_slot_contents(nullptr));

    // Staging writes the named slot without activating it, so the active slot must not move.
    REQUIRE(mod::set_save_slot_contents(1, "[YCD Version: 1]\nSaveSlot\n{staged}"));
    REQUIRE(mth::test::fake_save_state().staged_slot == 1);
    REQUIRE(mth::test::fake_save_state().staged_contents == "[YCD Version: 1]\nSaveSlot\n{staged}");
    REQUIRE(mod::active_save_slot() == 3);
    REQUIRE_FALSE(mod::set_save_slot_contents(1, nullptr));

    REQUIRE(mth::test::fake_save_state().restore_calls == 0);
    mod::player_restore_from_save();
    REQUIRE(mth::test::fake_save_state().restore_calls == 1);

    mod::set_save_write_enabled(false);
    REQUIRE_FALSE(mod::save_write_enabled());
    mod::set_save_write_enabled(true);
    REQUIRE(mod::save_write_enabled());

    mod::set_api(nullptr);
}

TEST_CASE("mod: player_component serves the game's live Player, null once it is torn down", "[mod][player]")
{
    mth::test::recorder().reset();
    mod::set_api(nullptr);
    REQUIRE_FALSE(mod::player_component_available());
    REQUIRE(mod::player_component() == nullptr);

    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);
    REQUIRE(mod::player_component_available());

    // No live player yet: the game's global is null before the first Player is built.
    REQUIRE(mod::player_component() == nullptr);

    int live = 0;
    mth::test::recorder().player = &live;
    REQUIRE(mod::player_component() == &live);

    // Player::~Player nulls the game's global; a ctor-captured pointer would still read &live here (#157).
    mth::test::recorder().player = nullptr;
    REQUIRE(mod::player_component() == nullptr);

    mod::set_api(nullptr);
}

TEST_CASE("mod: active_save_slot normalizes the no-slot sentinel to -1", "[mod]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);

    mth::test::fake_save_state().active_slot = 3;
    REQUIRE(mod::active_save_slot() == 3);

    // The game reports 10 (one past the last valid slot) when nothing is bound.
    mth::test::fake_save_state().active_slot = 10;
    REQUIRE(mod::active_save_slot() == -1);

    mth::test::fake_save_state().active_slot = -5;
    REQUIRE(mod::active_save_slot() == -1);

    mod::set_api(nullptr);
    REQUIRE(mod::active_save_slot() == -1);
}

TEST_CASE("mod: entries outside the game .text range are not called", "[mod]")
{
    TextRangeGuard range_guard;
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mth::test::recorder().revision = 4242;
    mod::set_api(&fake);

    // No range published: fail open, the fake's entries are honored.
    pal::set_game_text_range(pal::TextRange{});
    REQUIRE(mod::game_revision() == 4242);

    // A range that deliberately excludes the test binary's own code.
    pal::set_game_text_range(pal::TextRange{0x1000, 0x10});
    REQUIRE(mod::game_revision() == 0);
    REQUIRE(mod::active_save_slot() == -1);
    REQUIRE_FALSE(mod::player_die());

    mod::set_api(nullptr);
}
