#include <catch2/catch_test_macros.hpp>

#include "mth/core/ap/inbound_key.hpp"

TEST_CASE("inbound_state_key: seed and slot both participate in identity", "[mth][inbound_key]")
{
    REQUIRE(mth::inbound_state_key("SEEDA", 1) == "ap_SEEDA_1.state");
    // A different seed or slot must produce a different durable-state key.
    REQUIRE(mth::inbound_state_key("SEEDA", 1) != mth::inbound_state_key("SEEDB", 1));
    REQUIRE(mth::inbound_state_key("SEEDA", 1) != mth::inbound_state_key("SEEDA", 2));
}

TEST_CASE("inbound_needs_rebuild: initial build when nothing is loaded yet", "[mth][inbound_key]")
{
    REQUIRE(mth::inbound_needs_rebuild(false, "", "ap_SEEDA_1.state"));
}

TEST_CASE("inbound_needs_rebuild: no rebuild when reconnecting to the same seed/slot", "[mth][inbound_key]")
{
    const std::string key = mth::inbound_state_key("SEEDA", 1);
    REQUIRE_FALSE(mth::inbound_needs_rebuild(true, key, key));
}

TEST_CASE("inbound_needs_rebuild: rebuild when connecting to a different server (#124)", "[mth][inbound_key]")
{
    // The #124 regression: inbound was already built for server A, then a fresh connect to
    // server B must rebuild so the resend flushes B's checked-set, not A's stale one.
    const std::string a = mth::inbound_state_key("SEEDA", 1);
    const std::string b = mth::inbound_state_key("SEEDB", 3);
    REQUIRE(mth::inbound_needs_rebuild(true, a, b));
}
