// Characterization of parse_slot_data: what the client makes of a server slot_data blob, degenerate
// corners included. Malformed input fails closed by design.
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "mth/core/ap/ap_ids.hpp"
#include "mth/core/ap/slot_data.hpp"
#include "mth/core/fountain_lamps.hpp"
#include "mth/core/goal_state.hpp"
#include "pal/pal_log.hpp"

using nlohmann::json;

namespace
{

// Warnings are part of the contract here (an out-of-range index is dropped *and* reported), so the
// suite reads them back through the ILog seam.
class CapturingLog final : public pal::ILog
{
  public:
    void write(pal::LogLevel level, std::string_view message) override
    {
        std::lock_guard<std::mutex> lock(mu_);
        lines_.emplace_back(level, std::string(message));
    }
    void flush() override
    {
    }

    [[nodiscard]] bool saw(pal::LogLevel level, std::string_view needle) const
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (const auto &[lvl, text] : lines_)
            if (lvl == level && text.find(needle) != std::string::npos)
                return true;
        return false;
    }
    [[nodiscard]] std::size_t count() const
    {
        std::lock_guard<std::mutex> lock(mu_);
        return lines_.size();
    }

  private:
    mutable std::mutex mu_;
    std::vector<std::pair<pal::LogLevel, std::string>> lines_;
};

// RAII around the process-wide log seam so a throwing REQUIRE cannot leave it dangling.
class LogCapture
{
  public:
    LogCapture()
    {
        pal::set_default_log(&sink_);
    }
    ~LogCapture()
    {
        pal::set_default_log(nullptr);
    }
    LogCapture(const LogCapture &) = delete;
    LogCapture &operator=(const LogCapture &) = delete;

    const CapturingLog &sink() const
    {
        return sink_;
    }

  private:
    CapturingLog sink_;
};

} // namespace

TEST_CASE("parse_slot_data: an absent blob yields every default", "[slot_data]")
{
    const mth::SlotDataConfig cfg = mth::parse_slot_data(json{});

    REQUIRE_FALSE(cfg.ossex_start);
    REQUIRE(cfg.kear_mode == mth::KearMode::ApItems);
    REQUIRE_FALSE(cfg.burrow_rando);
    REQUIRE_FALSE(cfg.swim_rando);
    REQUIRE_FALSE(cfg.rope_rando);
    REQUIRE_FALSE(cfg.puff_rando);
    REQUIRE_FALSE(cfg.spring_rando);
    REQUIRE_FALSE(cfg.carry_rando);
    // Asymmetric with the empty-object case below: train_rando's "absent means on" default only
    // applies to a missing KEY, because the is_object guard short-circuits ahead of it.
    REQUIRE_FALSE(cfg.train_rando);
    REQUIRE(cfg.train_pass_cost == mth::kTrainPassCostDefault);
    REQUIRE_FALSE(cfg.deathlink);
    REQUIRE(cfg.max_stat_level == 99);
    REQUIRE(cfg.goal_config == 0);
    REQUIRE(cfg.goal_generators == 99);
    REQUIRE(cfg.goal_bosses == 99);
    REQUIRE(cfg.broken_generator_mask == mth::kAllGeneratorsMask);
    REQUIRE_FALSE(cfg.wallet_cap);
    REQUIRE(cfg.lit_generator_lamp_mask == 0);
    REQUIRE(cfg.removed_locations.empty());
}

TEST_CASE("parse_slot_data: an empty object takes the key-absent defaults", "[slot_data]")
{
    const mth::SlotDataConfig cfg = mth::parse_slot_data(json::object());

    REQUIRE(cfg.kear_mode == mth::KearMode::ApItems);
    REQUIRE(cfg.train_rando); // seeds predating the option still shuffle tickets
    REQUIRE(cfg.train_pass_cost == mth::kTrainPassCostDefault);
    REQUIRE(cfg.max_stat_level == 99);
    REQUIRE(cfg.goal_generators == 99);
    REQUIRE(cfg.goal_bosses == 99);
    REQUIRE(cfg.broken_generator_mask == mth::kAllGeneratorsMask);
}

TEST_CASE("parse_slot_data: a non-object blob is ignored wholesale", "[slot_data]")
{
    // Not merely "no keys": every read is guarded on is_object, so an array or scalar never throws.
    for (const json &blob : {json::array({1, 2}), json("nope"), json(7), json(true)})
    {
        const mth::SlotDataConfig cfg = mth::parse_slot_data(blob);
        REQUIRE(cfg.kear_mode == mth::KearMode::ApItems);
        REQUIRE_FALSE(cfg.train_rando); // see the absent-blob case: the guard beats the default
        REQUIRE(cfg.max_stat_level == 99);
        REQUIRE(cfg.broken_generator_mask == mth::kAllGeneratorsMask);
        REQUIRE(cfg.removed_locations.empty());
    }
}

TEST_CASE("parse_slot_data: the ability flags read as 0/non-0", "[slot_data]")
{
    const json data = {{"ossex_start", 1}, {"burrow_rando", 1}, {"swim_rando", 0}, {"rope_rando", 1}, {"puff_rando", 0},       {"spring_rando", 1},
                       {"carry_rando", 1}, {"train_rando", 0},  {"death_link", 1}, {"wallet_cap", 1}, {"train_pass_cost", 250}};
    const mth::SlotDataConfig cfg = mth::parse_slot_data(data);

    REQUIRE(cfg.ossex_start);
    REQUIRE(cfg.burrow_rando);
    REQUIRE_FALSE(cfg.swim_rando);
    REQUIRE(cfg.rope_rando);
    REQUIRE_FALSE(cfg.puff_rando);
    REQUIRE(cfg.spring_rando);
    REQUIRE(cfg.carry_rando);
    REQUIRE_FALSE(cfg.train_rando);
    REQUIRE(cfg.deathlink);
    REQUIRE(cfg.wallet_cap);
    REQUIRE(cfg.train_pass_cost == 250);
}

TEST_CASE("parse_slot_data: any non-negative-zero flag value counts as set", "[slot_data]")
{
    REQUIRE(mth::parse_slot_data(json{{"ossex_start", 2}}).ossex_start);
    REQUIRE(mth::parse_slot_data(json{{"ossex_start", -1}}).ossex_start);
}

TEST_CASE("parse_slot_data: kear_rando maps to a mode, unknown values to ApItems", "[slot_data]")
{
    REQUIRE(mth::parse_slot_data(json{{"kear_rando", 0}}).kear_mode == mth::KearMode::Vanilla);
    REQUIRE(mth::parse_slot_data(json{{"kear_rando", 1}}).kear_mode == mth::KearMode::ApItems);
    REQUIRE(mth::parse_slot_data(json{{"kear_rando", 2}}).kear_mode == mth::KearMode::AreaApItems);
    REQUIRE(mth::parse_slot_data(json{{"kear_rando", 7}}).kear_mode == mth::KearMode::ApItems);
    REQUIRE(mth::parse_slot_data(json{{"kear_rando", -3}}).kear_mode == mth::KearMode::ApItems);
}

TEST_CASE("parse_slot_data: max_stat_level is clamped to 10..99", "[slot_data]")
{
    REQUIRE(mth::parse_slot_data(json{{"max_stat_level", 42}}).max_stat_level == 42);
    REQUIRE(mth::parse_slot_data(json{{"max_stat_level", 0}}).max_stat_level == 10);
    REQUIRE(mth::parse_slot_data(json{{"max_stat_level", -5}}).max_stat_level == 10);
    REQUIRE(mth::parse_slot_data(json{{"max_stat_level", 1000}}).max_stat_level == 99);
}

TEST_CASE("parse_slot_data: goal fields pass through unvalidated", "[slot_data]")
{
    const json data = {{"goal_config", 2}, {"goal_generators", 4}, {"goal_bosses", 6}};
    const mth::SlotDataConfig cfg = mth::parse_slot_data(data);
    REQUIRE(cfg.goal_config == 2);
    REQUIRE(cfg.goal_generators == 4);
    REQUIRE(cfg.goal_bosses == 6);

    // No range check: a goal_config the tracker does not know, or a count nothing can reach, lands as written.
    const mth::SlotDataConfig odd = mth::parse_slot_data(json{{"goal_config", 99}, {"goal_generators", -1}, {"goal_bosses", 500}});
    REQUIRE(odd.goal_config == 99);
    REQUIRE(odd.goal_generators == -1);
    REQUIRE(odd.goal_bosses == 500);
}

TEST_CASE("parse_slot_data: goal counts default high when the key is absent", "[slot_data]")
{
    // Defaulting high fails closed: goal completion is irreversible and releases this slot's items,
    // so an unsatisfiable goal beats one a missing key could complete spuriously.
    const mth::SlotDataConfig cfg = mth::parse_slot_data(json{{"goal_config", 1}});
    REQUIRE(cfg.goal_config == 1);
    REQUIRE(cfg.goal_generators == 99);
    REQUIRE(cfg.goal_bosses == 99);
}

TEST_CASE("parse_slot_data: a missing goal count is reported when that goal is selected", "[slot_data]")
{
    LogCapture gens;
    REQUIRE(mth::parse_slot_data(json{{"goal_config", mth::kGoalGenerators}, {"broken_generators", json::array({0})}}).goal_generators == 99);
    REQUIRE(gens.sink().saw(pal::LogLevel::Warn, "goal_generators"));

    LogCapture bosses;
    REQUIRE(mth::parse_slot_data(json{{"goal_config", mth::kGoalBosses}}).goal_bosses == 99);
    REQUIRE(bosses.sink().saw(pal::LogLevel::Warn, "goal_bosses"));
}

TEST_CASE("parse_slot_data: a missing goal count the goal does not use is quiet", "[slot_data]")
{
    // The unselected count and the game-clear goal both default to 99 harmlessly, so warning there
    // would fire on nearly every seed.
    LogCapture log;
    REQUIRE(mth::parse_slot_data(json{{"goal_config", mth::kGoalFinish}}).goal_generators == 99);
    REQUIRE(mth::parse_slot_data(json{{"goal_config", mth::kGoalBosses}, {"goal_bosses", 4}}).goal_generators == 99);
    REQUIRE(mth::parse_slot_data(json{{"goal_config", mth::kGoalGenerators}, {"goal_generators", 3}, {"broken_generators", json::array({0})}}).goal_bosses ==
            99);
    REQUIRE(log.sink().count() == 0);
}

TEST_CASE("parse_slot_data: a generator goal with nothing broken is reported", "[slot_data]")
{
    LogCapture log;
    const json data = {{"goal_config", mth::kGoalGenerators}, {"goal_generators", 1}, {"broken_generators", json::array()}};
    REQUIRE(mth::parse_slot_data(data).broken_generator_mask == 0);
    REQUIRE(log.sink().saw(pal::LogLevel::Warn, "broken_generators"));
}

TEST_CASE("parse_slot_data: an empty broken_generators is quiet under another goal", "[slot_data]")
{
    LogCapture log;
    REQUIRE(mth::parse_slot_data(json{{"goal_config", mth::kGoalFinish}, {"broken_generators", json::array()}}).broken_generator_mask == 0);
    REQUIRE(mth::parse_slot_data(json{{"goal_config", mth::kGoalBosses}, {"goal_bosses", 3}, {"broken_generators", json::array()}}).broken_generator_mask == 0);
    REQUIRE(log.sink().count() == 0);
}

TEST_CASE("parse_slot_data: broken_generators indices become save bits", "[slot_data]")
{
    // Index -> SaveSlot 0x290 bit is the kGeneratorSaveBit permutation, not identity.
    REQUIRE(mth::parse_slot_data(json{{"broken_generators", json::array({0})}}).broken_generator_mask == (std::uint64_t{1} << 2));
    REQUIRE(mth::parse_slot_data(json{{"broken_generators", json::array({0, 1})}}).broken_generator_mask == 0x6);
    REQUIRE(mth::parse_slot_data(json{{"broken_generators", json::array({5})}}).broken_generator_mask == (std::uint64_t{1} << 6));
}

TEST_CASE("parse_slot_data: an absent broken_generators counts every generator", "[slot_data]")
{
    REQUIRE(mth::parse_slot_data(json::object()).broken_generator_mask == mth::kAllGeneratorsMask);
    // A non-array value is treated as absent, not as an empty list.
    REQUIRE(mth::parse_slot_data(json{{"broken_generators", 3}}).broken_generator_mask == mth::kAllGeneratorsMask);
    REQUIRE(mth::parse_slot_data(json{{"broken_generators", nullptr}}).broken_generator_mask == mth::kAllGeneratorsMask);
}

TEST_CASE("parse_slot_data: an empty broken_generators counts no generator", "[slot_data]")
{
    // Only listed generators count toward the goal (#141), so an empty list lists nothing. Distinct
    // from absent, which counts every generator.
    REQUIRE(mth::parse_slot_data(json{{"broken_generators", json::array()}}).broken_generator_mask == 0);
}

TEST_CASE("parse_slot_data: out-of-range broken_generators are dropped and reported", "[slot_data]")
{
    LogCapture log;
    const mth::SlotDataConfig cfg = mth::parse_slot_data(json{{"broken_generators", json::array({0, 6, -1})}});

    REQUIRE(cfg.broken_generator_mask == (std::uint64_t{1} << 2)); // only index 0 survived
    REQUIRE(log.sink().saw(pal::LogLevel::Warn, "broken_generators: generator index 6"));
    REQUIRE(log.sink().saw(pal::LogLevel::Warn, "broken_generators: generator index -1"));
}

TEST_CASE("parse_slot_data: non-integer broken_generators entries are skipped silently", "[slot_data]")
{
    LogCapture log;
    const mth::SlotDataConfig cfg = mth::parse_slot_data(json{{"broken_generators", json::array({0, "1", 2.5, nullptr})}});

    REQUIRE(cfg.broken_generator_mask == (std::uint64_t{1} << 2));
    REQUIRE(log.sink().count() == 0); // unlike removed_locations, dropped entries here are not reported
}

TEST_CASE("parse_slot_data: lit_generators folds indices into a lamp mask", "[slot_data]")
{
    REQUIRE(mth::parse_slot_data(json{{"lit_generators", json::array({0, 2})}}).lit_generator_lamp_mask == 0x5);
    REQUIRE(mth::parse_slot_data(json{{"lit_generators", json::array()}}).lit_generator_lamp_mask == 0);
    REQUIRE(mth::parse_slot_data(json{{"lit_generators", 4}}).lit_generator_lamp_mask == 0);
}

TEST_CASE("parse_slot_data: out-of-range lit_generators are dropped and reported", "[slot_data]")
{
    LogCapture log;
    // 6 is the Radiant Manor prime lamp: a real index in the game, out of range for this option.
    const mth::SlotDataConfig cfg = mth::parse_slot_data(json{{"lit_generators", json::array({1, 6, -2})}});

    REQUIRE(cfg.lit_generator_lamp_mask == 0x2);
    REQUIRE(log.sink().saw(pal::LogLevel::Warn, "lit_generators: ignoring out-of-range lamp index 6"));
    REQUIRE(log.sink().saw(pal::LogLevel::Warn, "lit_generators: ignoring out-of-range lamp index -2"));
}

TEST_CASE("parse_slot_data: removed_locations keeps integer ids in order", "[slot_data]")
{
    LogCapture log;
    const mth::SlotDataConfig cfg = mth::parse_slot_data(json{{"removed_locations", json::array({500, 100, 300})}});

    REQUIRE(cfg.removed_locations == std::vector<std::int64_t>{500, 100, 300});
    REQUIRE(log.sink().saw(pal::LogLevel::Info, "removed_locations: seed prunes 3 location(s)"));
}

TEST_CASE("parse_slot_data: removed_locations drops non-integer entries and reports the count", "[slot_data]")
{
    LogCapture log;
    const mth::SlotDataConfig cfg = mth::parse_slot_data(json{{"removed_locations", json::array({1, "2", 3.5, nullptr, 4})}});

    REQUIRE(cfg.removed_locations == std::vector<std::int64_t>{1, 4});
    REQUIRE(log.sink().saw(pal::LogLevel::Warn, "removed_locations: dropped 3 non-integer entr(ies)"));
}

TEST_CASE("parse_slot_data: an absent or non-array removed_locations prunes nothing", "[slot_data]")
{
    LogCapture log;
    REQUIRE(mth::parse_slot_data(json::object()).removed_locations.empty());
    REQUIRE(mth::parse_slot_data(json{{"removed_locations", json::array()}}).removed_locations.empty());
    REQUIRE(mth::parse_slot_data(json{{"removed_locations", 12}}).removed_locations.empty());
    REQUIRE(log.sink().count() == 0);
}

TEST_CASE("parse_slot_data: a key of the wrong JSON type throws", "[slot_data]")
{
    // A wrong-typed key aborts the parse, and the connection with it, rather than substituting a
    // default and running a seed on configuration the apworld did not actually specify.
    REQUIRE_THROWS_AS(mth::parse_slot_data(json{{"ossex_start", "yes"}}), nlohmann::json::type_error);
    REQUIRE_THROWS_AS(mth::parse_slot_data(json{{"goal_generators", "4"}}), nlohmann::json::type_error);
    REQUIRE_THROWS_AS(mth::parse_slot_data(json{{"train_pass_cost", json::array({1})}}), nlohmann::json::type_error);
}

TEST_CASE("parse_slot_data: a boolean flag value is accepted", "[slot_data]")
{
    REQUIRE(mth::parse_slot_data(json{{"ossex_start", true}}).ossex_start);
    REQUIRE_FALSE(mth::parse_slot_data(json{{"ossex_start", false}}).ossex_start);
}

TEST_CASE("parse_slot_data: a fractional integer field truncates", "[slot_data]")
{
    REQUIRE(mth::parse_slot_data(json{{"train_pass_cost", 250.9}}).train_pass_cost == 250);
    REQUIRE(mth::parse_slot_data(json{{"max_stat_level", 42.7}}).max_stat_level == 42);
}

TEST_CASE("parse_slot_data: unknown keys are ignored", "[slot_data]")
{
    const mth::SlotDataConfig cfg = mth::parse_slot_data(json{{"future_option", 5}, {"ossex_start", 1}});
    REQUIRE(cfg.ossex_start);
    REQUIRE(cfg.train_rando);
}

TEST_CASE("parse_slot_data: mirror_switch_rando defaults off", "[slot_data]")
{
    // A seed that says nothing about the shortcut switches gets vanilla ones, since it cannot grant any.
    REQUIRE_FALSE(mth::parse_slot_data(json::object()).mirror_switch_rando);
    REQUIRE_FALSE(mth::parse_slot_data(json{{"mirror_switch_rando", 0}}).mirror_switch_rando);
    REQUIRE(mth::parse_slot_data(json{{"mirror_switch_rando", 1}}).mirror_switch_rando);
    REQUIRE_FALSE(mth::parse_slot_data(json(5)).mirror_switch_rando); // non-object blob reaches no seed
}
