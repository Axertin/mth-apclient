#include <catch2/catch_test_macros.hpp>

#include "mth/core/data/component_types.hpp" // MM_Rtti type ids: same hash, pinned to GetTypeId() bodies
#include "mth/core/hook_hash.hpp"

using mth::hookhash::hash64;

// The reference values are the movabs immediates read out of the game's own RunHooks dispatch sites,
// so this pins the implementation against the shipping binary rather than against itself.
static_assert(hash64("FixedUpdate") == 0xd7aa418c4961d6baULL);
static_assert(hash64("GameInit") == 0xcd6c390c7a5949a6ULL);
static_assert(hash64("GameShutdown") == 0xfa1005f0dfb0b999ULL);
static_assert(hash64("GameStateTransition") == 0xb9cbbdeee4f465aaULL);
static_assert(hash64("WorldConstruct") == 0xe7ce860f2892fb7eULL);
static_assert(hash64("WorldDestroy") == 0x6ef6ff73b71bee4eULL);
static_assert(hash64("WorldUpdate") == 0x2f3672e14c657ffdULL);
static_assert(hash64("WorldUpdateEnd") == 0x687c151ff01cd1f9ULL);
static_assert(hash64("IsItemCollected") == 0xf9fd1d4efa1e21c4ULL);

// Added 2026-08-05, located in r149150 by scanning for these constants.
static_assert(hash64("ItemsOnPickup") == 0xfc3dd61d893bb4efULL);
static_assert(hash64("ItemsOnPickupDone") == 0xc33f1d10e2025ea2ULL);
static_assert(hash64("PickupOnPickup") == 0x00ce3d82b0e25e35ULL);
static_assert(hash64("ShopItemRefresh") == 0x553c7634843b755bULL);
static_assert(hash64("AreaManagerNewArea") == 0x762f9cb2d804d61bULL);
static_assert(hash64("ChestConstruct") == 0x04588da3e185326cULL);

// One of the two input hooks the save takeover installs; the keyboard one is not pinned.
static_assert(hash64("ycControllerUpdate") == 0x7cc4f96a2b42b94aULL);

// The set also covers the hash's 12-byte block seam: "GameInit" (8) and "GameShutdown" (12) take the
// byte-wise tail alone, while "GameStateTransition" (19) takes one whole block plus a tail.

TEST_CASE("hook_hash separates names that differ slightly", "[hookhash]")
{
    REQUIRE(hash64("ItemsOnPickup") != hash64("ItemsOnPickupDone"));
    REQUIRE(hash64("WorldUpdate") != hash64("WorldUpdateEnd"));
    REQUIRE(hash64("") != hash64("WorldUpdate"));
}
