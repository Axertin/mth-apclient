#include <cstddef>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/data/game_layout.hpp"
#include "mth/features/name_key.hpp"

// Stand-ins for the game objects the name-key walk crosses. Only the offsets it reads are modelled, and
// those offsets come from the same layout constants name_key.hpp reads, so nothing here can catch one
// drifting on a game rebuild: they are pinned reverse-engineered facts, confirmed in game rather than
// tested. What is left below is the logic that sits on top of them, the key precedence and the null hops.
namespace
{

struct FakeDescriptor
{
    unsigned char pad[0x28]{};
    std::uint64_t name_key{0};
};
static_assert(offsetof(FakeDescriptor, name_key) == 0x28);

struct FakeSpawnPoint
{
    FakeDescriptor *descriptor{nullptr}; // *(obj), the fallback the game reads the key through
    unsigned char pad[mth::layout::kSpawnPointNameKeyOff - sizeof(FakeDescriptor *)]{};
    std::uint64_t name_key{0};
};
static_assert(offsetof(FakeSpawnPoint, name_key) == mth::layout::kSpawnPointNameKeyOff);

struct FakeEntity
{
    unsigned char pad[0x40]{};
    FakeSpawnPoint *spawn{nullptr};
};
static_assert(offsetof(FakeEntity, spawn) == 0x40);

struct FakeComponent
{
    unsigned char pad[mth::layout::kKeyBlockEntityRefOff]{};
    FakeEntity *entity{nullptr};
};
static_assert(offsetof(FakeComponent, entity) == mth::layout::kKeyBlockEntityRefOff);

} // namespace

TEST_CASE("object_name_key: the direct hash wins over the shared descriptor", "[name_key]")
{
    FakeDescriptor desc{};
    desc.name_key = 0xdead;
    FakeSpawnPoint sp{};
    sp.descriptor = &desc;
    sp.name_key = 0xbeef;

    CHECK(mth::object_name_key(&sp) == 0xbeef);

    sp.name_key = 0; // no direct hash: the descriptor several objects share answers instead
    CHECK(mth::object_name_key(&sp) == 0xdead);
}

TEST_CASE("name_key: a null hop anywhere in either chain reports 0", "[name_key]")
{
    CHECK(mth::object_name_key(nullptr) == 0);

    FakeSpawnPoint sp{}; // no direct hash and no descriptor
    CHECK(mth::object_name_key(&sp) == 0);

    FakeDescriptor desc{}; // descriptor present but itself keyless
    sp.descriptor = &desc;
    CHECK(mth::object_name_key(&sp) == 0);

    FakeComponent comp{}; // no entity ref: an unspawned or already-freed component
    CHECK(mth::component_name_key(&comp) == 0);

    FakeEntity ent{}; // entity with no spawn point
    comp.entity = &ent;
    CHECK(mth::component_name_key(&comp) == 0);
}
