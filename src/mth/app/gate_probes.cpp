#include "mth/app/gate_probes.hpp"

#include <span>

#include "mod/mod_api.hpp"
#include "mth/core/data/game_layout.hpp"
#include "mth/core/data/game_symbols.hpp"
#include "mth/core/data/game_tables.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_module.hpp"

namespace mth
{

namespace
{

// Symbols both platforms hook; pal::required_platform_symbols() supplies the rest, since the two
// compilers inline different functions. Derived from what the code actually resolves, NOT from
// game_symbols.hpp wholesale: that header declares both platforms' variants side by side, so
// requiring all of it would refuse everywhere. process_sdl_event is absent on purpose - without it
// the overlay drops to render-only, which is degraded rather than fatal.
constexpr const char *kRequiredCommonSymbols[] = {
    sym::activate_save_cheats,
    sym::activate_save_slot,
    sym::area_new_area,
    sym::boss_on_defeated_no_skeleton,
    sym::boss_trigger_death_sequence,
    sym::bounce_plant_collide,
    sym::bounce_plant_launch,
    sym::carry_get_closest,
    sym::game_fixed_update,
    sym::hub_fountain_bulb_update,
    sym::key_block_update,
    sym::mina_on_burrow_jump,
    sym::on_pickup,
    sym::on_pickup_done,
    sym::pawn_shop_on_npc_event,
    sym::physics_get_aabb,
    sym::pickup_init,
    sym::pickup_on_pickup,
    sym::player_ctor,
    sym::player_pickup_carryable,
    sym::player_rope_climb_start,
    sym::player_set_burrow_ground,
    sym::player_trackable_update,
    sym::player_update_stats,
    sym::profile_select_menu_update_state,
    sym::queue_destroy,
    sym::room_manager_update,
    sym::s_r_item_collection,
    sym::s_r_items,
    sym::save_manager,
    sym::save_manager_write_save_data,
    sym::save_slot_clear,
    sym::save_slot_init_gamestate,
    sym::set_cheat_applied,
    sym::set_item_collected,
    sym::shop_get,
    sym::shop_is_out_of_stock,
    sym::shop_item_refresh,
    sym::shop_set_cursor,
    sym::spring_bellows_collide,
    sym::text_set_color,
    sym::text_set_text,
    sym::title_screen_start_game,
    sym::title_screen_update_state,
    sym::toggle_cheat,
    sym::train_authority_on_npc_event,
    sym::water_is_in_deep_water,
};

// Informational only: refusing on an unfamiliar build would brick the mod on every game patch,
// including ones where nothing load-bearing moved.
constexpr unsigned int kValidatedRevisionMin = 148662;
constexpr unsigned int kValidatedRevisionMax = 148905;

int count_unresolved(std::span<const char *const> names, const char *group)
{
    int missing = 0;
    for (const char *n : names)
    {
        const std::uintptr_t addr = pal::resolve_game_symbol(n);
        if (addr == 0)
        {
            ++missing;
            pal::logf(pal::LogLevel::Error, "gate: [%s] MISSING %s", group, n);
        }
        else
        {
            pal::logf(pal::LogLevel::Debug, "gate: [%s] ok %s -> %p", group, n, reinterpret_cast<void *>(addr));
        }
    }
    pal::logf(missing == 0 ? pal::LogLevel::Info : pal::LogLevel::Error, "gate: [%s] %zu symbol(s), %d missing", group, names.size(), missing);
    return missing;
}

} // namespace

GateInputs run_static_gate_probes(unsigned int game_revision)
{
    GateInputs in;

    pal::logf(pal::LogLevel::Info, "gate: running startup validation (revision=r%u)", game_revision);

    // First: the native hooks carrying item grants and the collection redirect go through it.
    in.mod_api_present = mod::api_available();
    pal::logf(in.mod_api_present ? pal::LogLevel::Info : pal::LogLevel::Error, "gate: mod_api_present=%d", in.mod_api_present ? 1 : 0);

    // Side-effect-free and cached: validates without hooking, and warms the cache for the installs.
    const int missing = count_unresolved(kRequiredCommonSymbols, "common") + count_unresolved(pal::required_platform_symbols(), "platform");
    in.symbols_resolved = missing == 0;

    // resolve() is read-only and idempotent; the dummy-item patch runs later in the feature
    // installers, so both rows still hold the game's own data here.
    tables::resolve();
    const bool donor_ok = tables::item_row_shape_ok(layout::kDummyAssetDonor);
    const bool dummy_ok = tables::item_row_shape_ok(layout::kApDummyItemType);
    in.item_table_shape_ok = donor_ok && dummy_ok;
    pal::logf(in.item_table_shape_ok ? pal::LogLevel::Info : pal::LogLevel::Error, "gate: item_table_shape_ok=%d (donor[%d]=%d dummy[%d]=%d)",
              in.item_table_shape_ok ? 1 : 0, layout::kDummyAssetDonor, donor_ok ? 1 : 0, layout::kApDummyItemType, dummy_ok ? 1 : 0);

    // Beyond drift: the lock seeding path shifts a u64 by the bit index, so an out-of-range value
    // is UB and sets an unrelated lock's bit in the player's save.
    constexpr int kCollectionSampleCount = 32;
    in.layout_probes_ok = tables::collection_resolved() && tables::collection_shape_ok(kCollectionSampleCount);
    pal::logf(in.layout_probes_ok ? pal::LogLevel::Info : pal::LogLevel::Error, "gate: layout_probes_ok=%d (first %d collection rows)",
              in.layout_probes_ok ? 1 : 0, kCollectionSampleCount);

    in.revision_known = game_revision >= kValidatedRevisionMin && game_revision <= kValidatedRevisionMax;
    pal::logf(pal::LogLevel::Info, "gate: revision_known=%d (r%u, validated r%u..r%u)", in.revision_known ? 1 : 0, game_revision, kValidatedRevisionMin,
              kValidatedRevisionMax);

    pal::logf(pal::LogLevel::Info, "gate: static validation complete, %d symbol(s) missing", missing);
    return in;
}

} // namespace mth
