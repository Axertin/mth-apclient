#include "mth/core/ap/slot_data.hpp"

#include <cstddef>

#include <nlohmann/json.hpp>

#include "mth/core/fountain_lamps.hpp"
#include "mth/core/stat_cap_state.hpp" // clamp_max_stat_level
#include "pal/pal_log.hpp"

namespace mth
{

SlotDataConfig parse_slot_data(const nlohmann::json &data)
{
    SlotDataConfig cfg;
    const bool obj = data.is_object();

    cfg.ossex_start = obj && data.value("ossex_start", 0) != 0;
    // "kear_rando" is a mode, not a flag: 0 = vanilla (Universal Kear items grant real keys),
    // 1/2 = per-lock / per-area AP items. Absent (older seeds) -> the apworld's own default.
    cfg.kear_mode = kear_mode_from_slot_data(obj ? data.value("kear_rando", 1) : 1);
    cfg.burrow_rando = obj && data.value("burrow_rando", 0) != 0;
    cfg.swim_rando = obj && data.value("swim_rando", 0) != 0;
    cfg.rope_rando = obj && data.value("rope_rando", 0) != 0;
    cfg.puff_rando = obj && data.value("puff_rando", 0) != 0;
    cfg.spring_rando = obj && data.value("spring_rando", 0) != 0;
    cfg.carry_rando = obj && data.value("carry_rando", 0) != 0;
    // Default true until the apworld ships a train-rando option: current seeds omit the key but
    // still shuffle the tickets/Train Pass, so gating must be on. Send train_rando=0 to opt out.
    cfg.train_rando = obj && data.value("train_rando", 1) != 0;
    // What the station's donation machine asks for, when the apworld carries it as a location. The
    // vanilla 10000 is a grind for a check whose reward is elsewhere in the multiworld (#162).
    cfg.train_pass_cost = obj ? data.value("train_pass_cost", kTrainPassCostDefault) : kTrainPassCostDefault;
    cfg.deathlink = obj && data.value("death_link", 0) != 0;
    cfg.max_stat_level = clamp_max_stat_level(obj ? data.value("max_stat_level", 99) : 99);
    cfg.goal_config = obj ? data.value("goal_config", 0) : 0;
    cfg.goal_generators = obj ? data.value("goal_generators", 99) : 99;
    cfg.goal_bosses = obj ? data.value("goal_bosses", 99) : 99;
    cfg.wallet_cap = obj && data.value("wallet_cap", 0) != 0;
    cfg.mirror_switch_rando = obj && data.value("mirror_switch_rando", 1) != 0;

    // The fail-closed default is silent otherwise, so a seed nobody can finish looks like a working one.
    if (cfg.goal_config == kGoalGenerators && !data.contains("goal_generators"))
        pal::logf(pal::LogLevel::Warn, "goal_generators: absent while goal_config selects the generator goal; the goal falls back to %d and cannot be reached",
                  cfg.goal_generators);
    if (cfg.goal_config == kGoalBosses && !data.contains("goal_bosses"))
        pal::logf(pal::LogLevel::Warn, "goal_bosses: absent while goal_config selects the boss goal; the goal falls back to %d and cannot be reached",
                  cfg.goal_bosses);

    std::vector<int> lit_gen_indices;
    if (obj)
        if (auto lg = data.find("lit_generators"); lg != data.end() && lg->is_array())
            for (const auto &v : *lg)
                if (v.is_number_integer())
                    lit_gen_indices.push_back(v.get<int>());
    for (int i : lit_gen_indices)
        if (i < 0 || i >= kGeneratorLampCount)
            pal::logf(pal::LogLevel::Warn, "lit_generators: ignoring out-of-range lamp index %d (valid 0..%d)", i, kGeneratorLampCount - 1);
    cfg.lit_generator_lamp_mask = lit_mask_from_indices(lit_gen_indices);

    // Absent means every generator counts, so a missing key must not collapse to an empty array.
    if (obj)
        if (auto bg = data.find("broken_generators"); bg != data.end() && bg->is_array())
        {
            std::vector<int> broken_indices;
            for (const auto &v : *bg)
                if (v.is_number_integer())
                    broken_indices.push_back(v.get<int>());
            for (int i : broken_indices)
                if (i < 0 || i >= kGeneratorLampCount)
                    pal::logf(pal::LogLevel::Warn, "broken_generators: generator index %d is outside the expected 0..%d", i, kGeneratorLampCount - 1);
            cfg.broken_generator_mask = broken_generator_mask(broken_indices);
            // An empty (or wholly unusable) list is legitimate under any other goal.
            if (cfg.broken_generator_mask == 0 && cfg.goal_config == kGoalGenerators)
                pal::logf(pal::LogLevel::Warn,
                          "broken_generators: lists no generator that counts while goal_config selects the generator goal; the goal cannot be reached");
        }

    // Locations the apworld pruned from the pool (dungeons the generator goal never requires); absent means nothing was pruned.
    if (obj)
        if (auto rl = data.find("removed_locations"); rl != data.end() && rl->is_array())
        {
            for (const auto &v : *rl)
                if (v.is_number_integer())
                    cfg.removed_locations.push_back(v.get<std::int64_t>());
            if (cfg.removed_locations.size() != rl->size())
                pal::logf(pal::LogLevel::Warn, "removed_locations: dropped %zu non-integer entr(ies)", rl->size() - cfg.removed_locations.size());
        }
    if (!cfg.removed_locations.empty())
        pal::logf(pal::LogLevel::Info, "removed_locations: seed prunes %zu location(s)", cfg.removed_locations.size());

    return cfg;
}

} // namespace mth
