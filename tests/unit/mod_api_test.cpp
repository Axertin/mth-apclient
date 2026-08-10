#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "MinaModHooks.h"
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
// Address inside this binary's .text, used to build a plausible range for in_game_text().
void fake_text_anchor()
{
}
int g_pickup_seen_slot = -999;
bool suppress_all_pickups(int slot, int /*item_type*/, void * /*player*/)
{
    g_pickup_seen_slot = slot;
    return true;
}
bool g_destroy_fired = false;
void destroy_cb()
{
    g_destroy_fired = true;
}
void *g_constructed_chest = nullptr;
void note_chest(void *chest)
{
    g_constructed_chest = chest;
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

TEST_CASE("mod: player_bosses_defeated reflects the API, 0 when unset", "[mod]")
{
    mth::test::recorder().reset();
    mod::set_api(nullptr);
    REQUIRE_FALSE(mod::bosses_api_available());
    REQUIRE(mod::player_bosses_defeated() == 0);

    auto fake = mth::test::make_fake_api();
    mth::test::recorder().bosses_defeated = 0b1011;
    mod::set_api(&fake);
    REQUIRE(mod::bosses_api_available());
    REQUIRE(mod::player_bosses_defeated() == 0b1011);
    mod::set_api(nullptr);
}

TEST_CASE("mod: player_position reports the native position, false when unset", "[mod]")
{
    mth::test::recorder().reset();
    mod::set_api(nullptr);
    float p[3]{-1.0f, -1.0f, -1.0f};
    REQUIRE_FALSE(mod::player_position(p));

    auto fake = mth::test::make_fake_api();
    mth::test::recorder().pos[0] = 12.5f;
    mth::test::recorder().pos[1] = -3.25f;
    mth::test::recorder().pos[2] = 0.5f;
    mod::set_api(&fake);
    REQUIRE(mod::player_position(p));
    REQUIRE(p[0] == 12.5f);
    REQUIRE(p[1] == -3.25f);
    REQUIRE(p[2] == 0.5f);
    mod::set_api(nullptr);
}

TEST_CASE("mod: a named hook registers under its game-facing name", "[mod]")
{
    mth::test::recorder().reset();
    mod::set_api(nullptr);
    REQUIRE_FALSE(mod::install_items_on_pickup_hook(nullptr)); // no API: nothing armed, nothing crashes

    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);
    REQUIRE(mod::install_items_on_pickup_hook(&suppress_all_pickups));
    REQUIRE(mth::test::recorder().hooks.count("ItemsOnPickup") == 1);

    // Installed but never dispatched: the state that means this path is dead on the running build.
    const auto unfired = mod::unfired_hooks();
    REQUIRE(std::find(unfired.begin(), unfired.end(), std::string("ItemsOnPickup")) != unfired.end());

    mod::remove_all_hooks();
    mod::set_api(nullptr);
}

TEST_CASE("mod: a dispatched hook reaches the callback and leaves the unfired list", "[mod]")
{
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);
    REQUIRE(mod::install_items_on_pickup_hook(&suppress_all_pickups));

    g_pickup_seen_slot = -999;
    std::int32_t slot = 42;
    std::int32_t item_type = 7;
    ItemsOnPickupCtx ctx{};
    ctx.collectionIndex = &slot;
    ctx.itemType = &item_type;
    mth::test::recorder().hooks["ItemsOnPickup"](&ctx);

    REQUIRE(g_pickup_seen_slot == 42);
    REQUIRE(ctx.modHandled); // the callback asked to suppress the original

    const auto after = mod::unfired_hooks();
    REQUIRE(std::find(after.begin(), after.end(), std::string("ItemsOnPickup")) == after.end());

    mod::remove_all_hooks();
    mod::set_api(nullptr);
}

// The appended-entry guard: an entry added to the API struct after the running game build lies past
// the end of that build's shorter struct, so it must be gated on the revision BEFORE it is read.
TEST_CASE("mod: appended entries stay unavailable on a build that predates them", "[mod]")
{
    TextRangeGuard guard;
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);

    // A range covering this test binary, so in_game_text() does not fall back to its fail-open answer.
    pal::set_game_text_range(pal::TextRange{reinterpret_cast<std::uintptr_t>(&fake_text_anchor) - 0x100000, 0x200000});

    mth::test::recorder().revision = 148716; // predates the appended entries
    REQUIRE_FALSE(mod::queue_destroy_available());
    REQUIRE(mod::sym_addr("s_rItems") == nullptr);

    REQUIRE_FALSE(mod::set_item_collected_available());
    REQUIRE_FALSE(mod::set_item_collected(37, true, nullptr, nullptr));
    REQUIRE(mth::test::recorder().collected_calls == 0);

    int widget = 0;
    REQUIRE_FALSE(mod::text_color_available());
    REQUIRE_FALSE(mod::set_text_color(&widget, 0xC0806040u));
    REQUIRE(mth::test::recorder().text_color_calls == 0);

    REQUIRE_FALSE(mod::set_text(&widget, "Golden Kear"));
    REQUIRE(mod::text_of(&widget) == nullptr);
    REQUIRE(mth::test::recorder().text_set_calls == 0);

    // Five-way gate: an old revision must refuse the whole palette entry point, no call reaching the fake.
    int palette = 0;
    REQUIRE_FALSE(mod::palette_api_available());
    REQUIRE(mod::clone_palette(&palette) == nullptr);
    REQUIRE(mth::test::recorder().clone_palette_calls == 0);

    int listener = 0;
    mth::test::recorder().in_deep_water = true;
    REQUIRE_FALSE(mod::water_api_available());
    REQUIRE_FALSE(mod::water_is_in_deep_water(&listener, false));
    REQUIRE(mth::test::recorder().water_calls == 0);

    int physics = 0;
    float box[6]{-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
    REQUIRE_FALSE(mod::physics_get_aabb(&physics, box, false, 0u));
    REQUIRE(box[0] == -1.0f); // left untouched, so a caller cannot query on a garbage box
    REQUIRE(mth::test::recorder().aabb_calls == 0);

    int manager = 0;
    int carryable = 0;
    int overlap = 0;
    mth::test::recorder().closest_result = &carryable;
    REQUIRE(mod::closest_carryable(&manager, box, 3, 1.6f, &overlap, 0ull) == nullptr);
    REQUIRE(mth::test::recorder().closest_calls == 0);

    REQUIRE_FALSE(mod::player_stats_api_available());
    REQUIRE_FALSE(mod::player_update_stats());
    REQUIRE(mth::test::recorder().update_stats_calls == 0);

    int block = 0;
    mth::test::recorder().entity_lists["KeyBlock"] = {&block};
    void *live_world = mod::player_world();
    REQUIRE(live_world != nullptr); // the world accessor itself predates the appended block
    REQUIRE(mod::world_entity_list(live_world, "KeyBlock", nullptr, 0) == 0);
    REQUIRE(mth::test::recorder().entity_list_calls == 0);

    mod::set_api(nullptr);
}

TEST_CASE("mod: appended entries become available on a newer build", "[mod]")
{
    TextRangeGuard guard;
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);
    pal::set_game_text_range(pal::TextRange{reinterpret_cast<std::uintptr_t>(&fake_text_anchor) - 0x100000, 0x200000});

    mth::test::recorder().revision = 200000; // newer than any build lacking the entries
    REQUIRE(mod::queue_destroy_available());

    const int before = mth::test::fake_queue_destroys;
    int world = 0;
    int entity = 0;
    REQUIRE(mod::queue_destroy_entity(&world, &entity, false));
    REQUIRE(mth::test::fake_queue_destroys == before + 1);

    // The mangled -> plain mapping has to reach the API for the resolver path to work at all.
    REQUIRE(mod::sym_addr("s_rItems") != nullptr);
    REQUIRE(mod::sym_addr("NotAnExposedName") == nullptr);

    REQUIRE(mod::set_item_collected_available());
    REQUIRE(mod::set_item_collected(37, true, nullptr, nullptr));
    REQUIRE(mth::test::recorder().collected_calls == 1);
    REQUIRE(mth::test::recorder().collected_index == 37);
    REQUIRE(mth::test::recorder().collected_value);

    // Channel mapping: the packed word carries r in the low byte, which is MM_Color's own byte order.
    int widget = 0;
    REQUIRE(mod::text_color_available());
    REQUIRE(mod::set_text_color(&widget, 0xC0806040u));
    REQUIRE(mth::test::recorder().text_color_calls == 1);
    REQUIRE(mth::test::recorder().text_color_target == &widget);
    REQUIRE(mth::test::recorder().text_color[0] == 0x40); // r
    REQUIRE(mth::test::recorder().text_color[1] == 0x60); // g
    REQUIRE(mth::test::recorder().text_color[2] == 0x80); // b
    REQUIRE(mth::test::recorder().text_color[3] == 0xC0); // a

    // The widget pointer goes to the API unadjusted: it already IS the ycTextComponent.
    REQUIRE(mod::set_text(&widget, "Golden Kear"));
    REQUIRE(mth::test::recorder().text_set_calls == 1);
    REQUIRE(mth::test::recorder().text_set_target == &widget);
    const char *read_back = mod::text_of(&widget);
    REQUIRE(read_back != nullptr);
    REQUIRE(std::string(read_back) == "Golden Kear");

    // Five-way gate: all five palette entries present -> available, and each call reaches the fake.
    REQUIRE(mod::palette_api_available());
    int source_palette = 0;
    int clone_target = 0;
    mth::test::recorder().clone_palette_result = &clone_target;
    REQUIRE(mod::clone_palette(&source_palette) == &clone_target);
    REQUIRE(mth::test::recorder().clone_palette_calls == 1);
    REQUIRE(mth::test::recorder().clone_palette_source == &source_palette);

    mod::palette_set_group(&clone_target, -1);
    REQUIRE(mth::test::recorder().palette_set_group_calls == 1);
    REQUIRE(mth::test::recorder().palette_set_group_value == -1);

    mod::palette_write_index(&clone_target, 3, 0xC0806040u);
    REQUIRE(mth::test::recorder().palette_write_index_calls == 1);
    REQUIRE(mth::test::recorder().palette_write_index_target == &clone_target);
    REQUIRE(mth::test::recorder().palette_write_index_index == 3);
    REQUIRE(mth::test::recorder().palette_write_index_color[0] == 0x40); // r

    mth::test::recorder().palette_get_index_result = 7;
    REQUIRE(mod::palette_get_index(&clone_target, 0xC0806040u) == 7);
    REQUIRE(mth::test::recorder().palette_get_index_calls == 1);

    mth::test::recorder().palette_get_width_result = 16;
    REQUIRE(mod::palette_get_width(&clone_target) == 16);
    REQUIRE(mth::test::recorder().palette_get_width_calls == 1);

    int listener = 0;
    mth::test::recorder().in_deep_water = true;
    REQUIRE(mod::water_api_available());
    REQUIRE(mod::water_is_in_deep_water(&listener, false));
    REQUIRE(mth::test::recorder().water_calls == 1);
    REQUIRE(mth::test::recorder().water_target == &listener);
    REQUIRE_FALSE(mth::test::recorder().water_ignore_enabled);
    mth::test::recorder().in_deep_water = false;
    REQUIRE_FALSE(mod::water_is_in_deep_water(&listener, true));
    REQUIRE(mth::test::recorder().water_ignore_enabled);

    // Six floats in, six floats out, in the game AABB's own order: center.xyz then half-extent.xyz.
    int physics = 0;
    float box[6]{};
    REQUIRE(mod::physics_get_aabb(&physics, box, true, 5u));
    REQUIRE(mth::test::recorder().aabb_calls == 1);
    REQUIRE(mth::test::recorder().aabb_target == &physics);
    REQUIRE(mth::test::recorder().aabb_local);
    REQUIRE(mth::test::recorder().aabb_shape_flags == 5u);
    for (int i = 0; i < 6; ++i)
        REQUIRE(box[i] == static_cast<float>(i + 1));

    // The same six floats have to survive the by-value AABB the carry query takes.
    int manager = 0;
    int carryable = 0;
    int overlap = 0;
    mth::test::recorder().closest_result = &carryable;
    REQUIRE(mod::closest_carryable(&manager, box, 3, 1.6f, &overlap, 0x9ull) == &carryable);
    REQUIRE(mth::test::recorder().closest_calls == 1);
    REQUIRE(mth::test::recorder().closest_target == &manager);
    for (int i = 0; i < 6; ++i)
        REQUIRE(mth::test::recorder().closest_box[i] == static_cast<float>(i + 1));
    REQUIRE(mth::test::recorder().closest_layer == 3);
    REQUIRE(mth::test::recorder().closest_max_dist == 1.6f);
    REQUIRE(mth::test::recorder().closest_mask == 0x9ull);
    REQUIRE(overlap == 1);

    mth::test::recorder().closest_result = nullptr;
    REQUIRE(mod::closest_carryable(&manager, box, 3, 1.6f, &overlap, 0ull) == nullptr);
    REQUIRE(mth::test::recorder().closest_calls == 2);

    REQUIRE(mod::player_stats_api_available());
    REQUIRE(mod::player_update_stats());
    REQUIRE(mth::test::recorder().update_stats_calls == 1);

    // The entity list sizes itself: a null/0 call reports the total, then the fill call reads it back.
    int block_a = 0;
    int block_b = 0;
    mth::test::recorder().entity_lists["KeyBlock"] = {&block_a, &block_b};
    void *live_world = mod::player_world();
    REQUIRE(live_world != nullptr);
    REQUIRE(mod::world_entity_list(live_world, "KeyBlock", nullptr, 0) == 2);
    void *entities[2]{};
    REQUIRE(mod::world_entity_list(live_world, "KeyBlock", entities, 2) == 2);
    REQUIRE(entities[0] == &block_a);
    REQUIRE(entities[1] == &block_b);

    // The total comes back even when the buffer is short, which is what makes the sizing call safe.
    void *one[1]{};
    REQUIRE(mod::world_entity_list(live_world, "KeyBlock", one, 1) == 2);
    REQUIRE(one[0] == &block_a);

    // A list the world does not carry (there is no "Chest" list) reports nothing.
    REQUIRE(mod::world_entity_list(live_world, "Chest", nullptr, 0) == 0);
    REQUIRE(mod::world_entity_list(nullptr, "KeyBlock", nullptr, 0) == 0);
    REQUIRE(mod::world_entity_list(live_world, nullptr, nullptr, 0) == 0);

    mod::set_api(nullptr);
}

TEST_CASE("mod: ChestConstruct hands the callback the ctx's Chest", "[mod]")
{
    mth::test::recorder().reset();
    mod::set_api(nullptr);
    REQUIRE_FALSE(mod::install_chest_construct_hook(&note_chest)); // no API: nothing armed

    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);
    REQUIRE(mod::install_chest_construct_hook(&note_chest));
    REQUIRE(mth::test::recorder().hooks.count("ChestConstruct") == 1);

    // Installed but never dispatched: the state that means chests would never be registered.
    const auto unfired = mod::unfired_hooks();
    REQUIRE(std::find(unfired.begin(), unfired.end(), std::string("ChestConstruct")) != unfired.end());

    int chest = 0;
    ChestConstructCtx ctx{};
    ctx.chest = reinterpret_cast<Chest *>(&chest);
    g_constructed_chest = nullptr;
    mth::test::recorder().fire("ChestConstruct", &ctx);
    REQUIRE(g_constructed_chest == &chest); // the ctx carries the real Chest*, so no subobject fixup

    const auto after = mod::unfired_hooks();
    REQUIRE(std::find(after.begin(), after.end(), std::string("ChestConstruct")) == after.end());

    mod::remove_chest_construct_hook();
    mod::set_api(nullptr);
}

TEST_CASE("mod: weak pointers need no revision gate and report a dead target", "[mod]")
{
    TextRangeGuard guard;
    mth::test::recorder().reset();
    mod::set_api(nullptr);
    REQUIRE_FALSE(mod::weak_ptr_api_available());
    REQUIRE(mod::weak_ptr_create(nullptr) == nullptr);

    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);
    pal::set_game_text_range(pal::TextRange{reinterpret_cast<std::uintptr_t>(&fake_text_anchor) - 0x100000, 0x200000});

    // The weak-pointer entries predate the appended block, so an old build still has them.
    mth::test::recorder().revision = 148716;
    REQUIRE(mod::weak_ptr_api_available());

    int live = 0;
    void *weak = mod::weak_ptr_create(&live);
    REQUIRE(weak != nullptr);
    REQUIRE(mod::weak_ptr_get(weak) == &live);

    mth::test::recorder().weak_targets[0] = nullptr; // the game freed the target
    REQUIRE(mod::weak_ptr_get(weak) == nullptr);

    mod::weak_ptr_destroy(weak);
    REQUIRE(mth::test::recorder().weak_destroys == 1);

    REQUIRE(mod::weak_ptr_create(nullptr) == nullptr); // never hands the game a null target
    REQUIRE(mod::weak_ptr_get(nullptr) == nullptr);
    mod::weak_ptr_destroy(nullptr); // no-op, not a call
    REQUIRE(mth::test::recorder().weak_destroys == 1);

    mod::set_api(nullptr);
}

// The chest registry's sweep step: hold weak pointers, drop the ones whose target died, keep the rest.
// A raw pointer here would be a use-after-free the first time a freed chest was written through.
TEST_CASE("mod: a weak-pointer registry drops exactly the dead entries", "[mod]")
{
    TextRangeGuard guard;
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);
    pal::set_game_text_range(pal::TextRange{reinterpret_cast<std::uintptr_t>(&fake_text_anchor) - 0x100000, 0x200000});

    int a = 0;
    int b = 0;
    int c = 0;
    std::vector<void *> registry{mod::weak_ptr_create(&a), mod::weak_ptr_create(&b), mod::weak_ptr_create(&c)};
    REQUIRE(mth::test::recorder().weak_creates == 3);

    mth::test::recorder().weak_targets[1] = nullptr; // b died between two sweeps

    std::vector<void *> alive;
    for (std::size_t i = 0; i < registry.size();)
    {
        void *target = mod::weak_ptr_get(registry[i]);
        if (target == nullptr)
        {
            mod::weak_ptr_destroy(registry[i]);
            registry[i] = registry.back();
            registry.pop_back();
            continue;
        }
        alive.push_back(target);
        ++i;
    }

    REQUIRE(registry.size() == 2);
    REQUIRE(mth::test::recorder().weak_destroys == 1); // exactly the dead one was released
    REQUIRE(std::find(alive.begin(), alive.end(), static_cast<void *>(&a)) != alive.end());
    REQUIRE(std::find(alive.begin(), alive.end(), static_cast<void *>(&c)) != alive.end());
    REQUIRE(std::find(alive.begin(), alive.end(), static_cast<void *>(&b)) == alive.end());

    mod::set_api(nullptr);
}

TEST_CASE("mod: player_world serves the game's live world, null when unset", "[mod]")
{
    mth::test::recorder().reset();
    mod::set_api(nullptr);
    REQUIRE(mod::player_world() == nullptr);

    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);
    REQUIRE(mod::player_world() != nullptr);
    mod::set_api(nullptr);
}

TEST_CASE("mod: queue_destroy_entity refuses null world or entity", "[mod]")
{
    TextRangeGuard guard;
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);
    pal::set_game_text_range(pal::TextRange{reinterpret_cast<std::uintptr_t>(&fake_text_anchor) - 0x100000, 0x200000});
    mth::test::recorder().revision = 200000;

    int x = 0;
    REQUIRE_FALSE(mod::queue_destroy_entity(nullptr, &x, false));
    REQUIRE_FALSE(mod::queue_destroy_entity(&x, nullptr, false));
    mod::set_api(nullptr);
}

TEST_CASE("mod: set_item_collected and set_text_color refuse bad arguments", "[mod]")
{
    TextRangeGuard guard;
    mth::test::recorder().reset();
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);
    pal::set_game_text_range(pal::TextRange{reinterpret_cast<std::uintptr_t>(&fake_text_anchor) - 0x100000, 0x200000});
    mth::test::recorder().revision = 200000;

    REQUIRE_FALSE(mod::set_item_collected(-1, true, nullptr, nullptr)); // negative index would write an unrelated bit
    REQUIRE_FALSE(mod::set_text_color(nullptr, 0xFFFFFFFFu));
    REQUIRE(mth::test::recorder().collected_calls == 0);
    REQUIRE(mth::test::recorder().text_color_calls == 0);
    mod::set_api(nullptr);
}
