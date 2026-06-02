#include <catch2/catch_test_macros.hpp>

#include "mth/core/offsets.hpp"

TEST_CASE("offsets: Linux v1.0 table matches the analyzed addresses", "[mth][offsets]")
{
    // Real ELF symbol vaddrs (nm on the un-stripped binary), NOT Ghidra's
    // image-based (0x100000-biased) addresses. Build 958b6568 (1.0.5, 2026-06-02).
    const auto &o = mth::offsets_for(mth::Build::Linux_v1_0);
    REQUIRE(o.game_fixed_update == 0x00c97000);
    REQUIRE(o.game_update == 0x00c981e0);
    REQUIRE(o.world_update == 0x00ec6cb0);
    REQUIRE(o.update_queue == 0x0031f620);
}

TEST_CASE("offsets: unknown/unmapped builds are zeroed so GameHooks skips", "[mth][offsets]")
{
    const auto &unknown = mth::offsets_for(mth::Build::Unknown);
    REQUIRE(unknown.game_fixed_update == 0);
    REQUIRE(unknown.game_update == 0);
    REQUIRE(unknown.world_update == 0);
    REQUIRE(unknown.update_queue == 0);

    // Windows is a known enumerator but not mapped yet - also zeroed.
    REQUIRE(mth::offsets_for(mth::Build::Windows_v1_0).update_queue == 0);
}

TEST_CASE("offsets: lookup returns a stable reference per build", "[mth][offsets]")
{
    REQUIRE(&mth::offsets_for(mth::Build::Linux_v1_0) == &mth::offsets_for(mth::Build::Linux_v1_0));
    REQUIRE(&mth::offsets_for(mth::Build::Linux_v1_0) != &mth::offsets_for(mth::Build::Unknown));
}

TEST_CASE("rando offsets: Linux v1 mapped, unknown zeroed", "[mth][offsets]")
{
    REQUIRE(mth::rando_offsets_for(mth::Build::Linux_v1_0).on_pickup_done != 0);
    REQUIRE(mth::rando_offsets_for(mth::Build::Unknown).on_pickup_done == 0);
}

TEST_CASE("overlay offsets: Linux v1 ProcessSDLEvent mapped, others zeroed", "[mth][offsets]")
{
    REQUIRE(mth::overlay_offsets_for(mth::Build::Linux_v1_0).process_sdl_event == 0x0020cca0);
    REQUIRE(mth::overlay_offsets_for(mth::Build::Unknown).process_sdl_event == 0);
    REQUIRE(mth::overlay_offsets_for(mth::Build::Windows_v1_0).process_sdl_event == 0);
}
