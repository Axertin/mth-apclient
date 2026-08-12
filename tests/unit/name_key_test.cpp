#include <cstddef>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/data/game_layout.hpp"
#include "mth/features/name_key.hpp"

// Stand-ins for the game objects the name-key walk crosses. Only the offsets it reads are modelled; the
// static_asserts keep the fakes pinned to the real layout constants.
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

TEST_CASE("object_name_key: the direct hash wins", "[name_key]")
{
    FakeDescriptor desc{};
    desc.name_key = 0xdead;
    FakeSpawnPoint sp{};
    sp.descriptor = &desc;
    sp.name_key = 0xbeef;

    CHECK(mth::object_name_key(&sp) == 0xbeef);
}

TEST_CASE("object_name_key: falls back to the shared descriptor when the direct hash is 0", "[name_key]")
{
    FakeDescriptor desc{};
    desc.name_key = 0xdead;
    FakeSpawnPoint sp{};
    sp.descriptor = &desc;

    CHECK(mth::object_name_key(&sp) == 0xdead);
}

TEST_CASE("object_name_key: a broken chain reports 0 rather than dereferencing it", "[name_key]")
{
    CHECK(mth::object_name_key(nullptr) == 0);

    FakeSpawnPoint sp{}; // no direct hash and no descriptor
    CHECK(mth::object_name_key(&sp) == 0);

    FakeDescriptor desc{}; // descriptor present but itself keyless
    sp.descriptor = &desc;
    CHECK(mth::object_name_key(&sp) == 0);
}

TEST_CASE("component_name_key: walks the entity ref to the spawn point's hash", "[name_key]")
{
    FakeSpawnPoint sp{};
    sp.name_key = 0xc0ffee;
    FakeEntity ent{};
    ent.spawn = &sp;
    FakeComponent comp{};
    comp.entity = &ent;

    CHECK(mth::component_name_key(&comp) == 0xc0ffee);
}

TEST_CASE("component_name_key: a null hop anywhere in the chain reports 0", "[name_key]")
{
    FakeComponent comp{}; // no entity ref: an unspawned or already-freed component
    CHECK(mth::component_name_key(&comp) == 0);

    FakeEntity ent{}; // entity with no spawn point
    comp.entity = &ent;
    CHECK(mth::component_name_key(&comp) == 0);
}
