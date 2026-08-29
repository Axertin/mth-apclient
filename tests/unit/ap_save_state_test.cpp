#include <optional>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/ap/ap_save_state.hpp"

namespace
{
// A state with no backing store: the format is what these cases exercise.
mth::ApSaveState detached()
{
    return mth::ApSaveState([] { return std::nullopt; }, [](std::string_view) {});
}
} // namespace

TEST_CASE("state serializes to the line-oriented format", "[ap_save_state]")
{
    auto s = detached();
    s.mark_checked(2);
    s.mark_checked(1);
    s.mark_granted(7);
    s.set_game_slot(3);
    REQUIRE(s.serialize() == "c 1\nc 2\ng 7\ns 3\n");
    REQUIRE(s.checked() == std::set<int>{1, 2}); // the set the bridge walks to replay locations
}

TEST_CASE("state omits an unset game slot", "[ap_save_state]")
{
    auto s = detached();
    s.mark_checked(4);
    REQUIRE(s.serialize() == "c 4\n");
}

TEST_CASE("state round-trips through serialize and deserialize", "[ap_save_state]")
{
    auto a = detached();
    a.mark_checked(10);
    a.mark_granted(20);
    a.set_game_slot(1);

    auto b = detached();
    b.deserialize(a.serialize());
    REQUIRE(b.is_checked(10));
    REQUIRE(b.is_granted(20));
    REQUIRE(b.game_slot() == 1);
}

TEST_CASE("state tolerates a truncated trailing record", "[ap_save_state]")
{
    auto s = detached();
    s.deserialize("c 1\ng 2\nc "); // interrupted write
    REQUIRE(s.is_checked(1));
    REQUIRE(s.is_granted(2));
}

TEST_CASE("state loads through the injected loader", "[ap_save_state]")
{
    mth::ApSaveState s([] { return std::optional<std::string>("c 5\ns 2\n"); }, [](std::string_view) {});
    REQUIRE(s.is_checked(5));
    REQUIRE(s.game_slot() == 2);
}

TEST_CASE("state save hands the serialized text to the injected store", "[ap_save_state]")
{
    std::string written;
    mth::ApSaveState s([] { return std::nullopt; }, [&written](std::string_view t) { written = std::string(t); });
    s.mark_checked(9);
    s.stage();
    REQUIRE_FALSE(written.empty());

    const std::string first = written;
    s.mark_granted(4);
    s.stage();
    REQUIRE(written != first);
}
