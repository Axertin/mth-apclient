#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/kear_completion.hpp"

namespace
{
std::vector<mth::ReceivedItem> items(const std::vector<std::int64_t> &ids)
{
    std::vector<mth::ReceivedItem> v;
    int idx = 0;
    for (std::int64_t id : ids)
        v.push_back(mth::ReceivedItem{id, idx++, 1, 0});
    return v;
}

std::vector<std::int64_t> all_single_kears()
{
    return std::vector<std::int64_t>(std::begin(mth::kSingleKearItemIds), std::end(mth::kSingleKearItemIds));
}

std::vector<std::int64_t> all_area_kears()
{
    std::vector<std::int64_t> v;
    for (std::int64_t id = mth::kAreaKearFirstItemId; id <= mth::kAreaKearLastItemId; ++id)
        v.push_back(id);
    return v;
}
} // namespace

// Universal Kears are fungible and all share one id, so vanilla mode counts receipts.
TEST_CASE("have_all_kears counts vanilla kear receipts", "[kear]")
{
    const std::int64_t kear = mth::ap_item_id(mth::kUniversalKearItemType);
    REQUIRE_FALSE(mth::have_all_kears(mth::KearMode::Vanilla, items(std::vector<std::int64_t>(49, kear))));
    REQUIRE(mth::have_all_kears(mth::KearMode::Vanilla, items(std::vector<std::int64_t>(50, kear))));
    REQUIRE(mth::have_all_kears(mth::KearMode::Vanilla, items(std::vector<std::int64_t>(51, kear))));
}

TEST_CASE("have_all_kears needs every single kear id", "[kear]")
{
    const auto ids = all_single_kears();
    REQUIRE(mth::have_all_kears(mth::KearMode::ApItems, items(ids)));

    auto missing = ids;
    missing.pop_back();
    REQUIRE_FALSE(mth::have_all_kears(mth::KearMode::ApItems, items(missing)));

    // A duplicate must not stand in for the absent one: this mode counts distinct ids, not receipts.
    missing.push_back(ids.front());
    REQUIRE(missing.size() == ids.size());
    REQUIRE_FALSE(mth::have_all_kears(mth::KearMode::ApItems, items(missing)));
}

TEST_CASE("have_all_kears needs every area kear id", "[kear]")
{
    const auto ids = all_area_kears();
    REQUIRE(mth::have_all_kears(mth::KearMode::AreaApItems, items(ids)));

    auto missing = ids;
    missing.erase(missing.begin());
    REQUIRE_FALSE(mth::have_all_kears(mth::KearMode::AreaApItems, items(missing)));

    missing.push_back(ids.back());
    REQUIRE(missing.size() == ids.size());
    REQUIRE_FALSE(mth::have_all_kears(mth::KearMode::AreaApItems, items(missing)));
}

// 2306 is the second engine lock the 2304 item removes; it never arrives as an item of its own.
TEST_CASE("have_all_kears ignores unrelated items", "[kear]")
{
    auto ids = all_single_kears();
    ids.push_back(2306);
    ids.push_back(mth::ap_item_id(9));
    REQUIRE(mth::have_all_kears(mth::KearMode::ApItems, items(ids)));

    REQUIRE_FALSE(mth::have_all_kears(mth::KearMode::ApItems, items({2306, mth::ap_item_id(9), 2500})));
}

// Mirrored from the apworld pools (worlds/mina_the_hollower/data/items/kears.py). A change there must
// fail here rather than silently making the check unobtainable or firing it early.
TEST_CASE("kear set sizes match the apworld pools", "[kear]")
{
    REQUIRE(mth::kSingleKearCount == 39);
    REQUIRE(mth::kAreaKearCount == 17);
    REQUIRE(mth::kVanillaKearTotal == 50);
}
