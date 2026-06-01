#include <catch2/catch_test_macros.hpp>

#include "mth/core/build_id.hpp"

TEST_CASE("build_id: known Linux build id maps to enumerator", "[mth][build_id]")
{
    REQUIRE(mth::detect_build(mth::kLinuxV1BuildId) == mth::Build::Linux_v1_0);
}

TEST_CASE("build_id: known Windows build id maps to enumerator", "[mth][build_id]")
{
    REQUIRE(mth::detect_build(mth::kWindowsV1BuildId) == mth::Build::Windows_v1_0);
}

TEST_CASE("build_id: unknown / empty IDs return Build::Unknown", "[mth][build_id]")
{
    REQUIRE(mth::detect_build("") == mth::Build::Unknown);
    REQUIRE(mth::detect_build("deadbeef") == mth::Build::Unknown);
}

TEST_CASE("build_id: build_name returns a non-empty label", "[mth][build_id]")
{
    REQUIRE(mth::build_name(mth::Build::Linux_v1_0) == "Linux v1.0");
    REQUIRE(mth::build_name(mth::Build::Windows_v1_0) == "Windows v1.0");
    REQUIRE(mth::build_name(mth::Build::Unknown) == "Unknown");
}
