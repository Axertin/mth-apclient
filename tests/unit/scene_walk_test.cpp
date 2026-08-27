#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "mocks/fake_mod_api.hpp"
#include "mod/mod_api.hpp"
#include "mth/core/data/component_types.hpp"
#include "mth/features/scene_walk.hpp"

namespace
{

constexpr std::uint64_t kTarget = 0xAAAA'0000'0000'0001ULL;
constexpr std::uint64_t kOther = 0xBBBB'0000'0000'0002ULL;

// mod_size 0 is the documented test path through looks_like_component: with no module range published
// there is nothing to range-check a vtable against, so the pointer checks stand alone.
struct Walker
{
    std::vector<void *> pending;
    std::vector<void *> buffer;

    template <typename V> mth::SceneWalk run(void *root, V &&v)
    {
        return mth::walk_scene(root, 0, 0, pending, buffer, std::forward<V>(v));
    }
};

} // namespace

TEST_CASE("scene walk: descends entities and reports leaves to the visitor", "[scene]")
{
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);
    mth::test::fake_scene().reset();
    auto &s = mth::test::fake_scene();

    void *root = s.add(mth::rtti::kYcEntity, nullptr);
    void *child_entity = s.add(mth::rtti::kYcEntity, root);
    void *shallow = s.add(kTarget, root);
    void *deep = s.add(kTarget, child_entity);
    s.add(kOther, child_entity);

    std::vector<void *> found;
    Walker w;
    const mth::SceneWalk r = w.run(root,
                                   [&](std::span<void *const> children)
                                   {
                                       for (void *c : children)
                                           if (mod::component_isa(c, kTarget))
                                               found.push_back(c);
                                       return true;
                                   });

    REQUIRE(found.size() == 2);
    REQUIRE((found[0] == shallow || found[0] == deep));
    REQUIRE((found[1] == shallow || found[1] == deep));
    REQUIRE(found[0] != found[1]);
    REQUIRE(r.visited == 4); // both entities plus both targets plus the other leaf, minus the root itself
    REQUIRE_FALSE(r.stopped_by_visitor);
    REQUIRE_FALSE(r.node_budget_spent);
    mod::set_api(nullptr);
}

TEST_CASE("scene walk: a visitor returning false stops the walk", "[scene]")
{
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);
    mth::test::fake_scene().reset();
    auto &s = mth::test::fake_scene();

    void *root = s.add(mth::rtti::kYcEntity, nullptr);
    void *branch = s.add(mth::rtti::kYcEntity, root);
    s.add(kTarget, root);
    s.add(kTarget, branch);

    int calls = 0;
    Walker w;
    const mth::SceneWalk r = w.run(root,
                                   [&](std::span<void *const>)
                                   {
                                       ++calls;
                                       return false;
                                   });

    REQUIRE(calls == 1);
    REQUIRE(r.stopped_by_visitor);
    REQUIRE(w.pending.empty()); // no game pointer outlives the walk, early exit included
    mod::set_api(nullptr);
}

TEST_CASE("scene walk: entities never reach the visitor", "[scene]")
{
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);
    mth::test::fake_scene().reset();
    auto &s = mth::test::fake_scene();

    void *root = s.add(mth::rtti::kYcEntity, nullptr);
    s.add(mth::rtti::kYcEntity, root);
    s.add(kOther, root);

    std::size_t seen = 0;
    bool saw_entity = false;
    Walker w;
    w.run(root,
          [&](std::span<void *const> children)
          {
              for (void *c : children)
              {
                  ++seen;
                  if (mod::component_isa(c, mth::rtti::kYcEntity))
                      saw_entity = true;
              }
              return true;
          });

    REQUIRE(seen == 1);
    REQUIRE_FALSE(saw_entity);
    mod::set_api(nullptr);
}

TEST_CASE("scene walk: a childless or null root walks nothing", "[scene]")
{
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);
    mth::test::fake_scene().reset();
    void *root = mth::test::fake_scene().add(mth::rtti::kYcEntity, nullptr);

    int calls = 0;
    Walker w;
    const auto visit = [&](std::span<void *const>)
    {
        ++calls;
        return true;
    };
    REQUIRE(w.run(root, visit).visited == 0);
    REQUIRE(w.run(nullptr, visit).visited == 0);
    REQUIRE(calls == 0);
    mod::set_api(nullptr);
}

TEST_CASE("scene walk: a node wider than the child cap is walked as a prefix and reported", "[scene]")
{
    auto fake = mth::test::make_fake_api();
    mod::set_api(&fake);
    mth::test::fake_scene().reset();
    auto &s = mth::test::fake_scene();

    void *root = s.add(mth::rtti::kYcEntity, nullptr);
    const std::size_t wide = mth::kSceneMaxChildren + 7;
    for (std::size_t i = 0; i < wide; ++i)
        s.add(kOther, root);

    std::size_t seen = 0;
    Walker w;
    const mth::SceneWalk r = w.run(root,
                                   [&](std::span<void *const> children)
                                   {
                                       seen += children.size();
                                       return true;
                                   });

    REQUIRE(seen == mth::kSceneMaxChildren); // a prefix, rather than the subtree being abandoned
    REQUIRE(r.widest_node == root);
    REQUIRE(r.widest_node_children == wide); // the true count, not the walked one
    mod::set_api(nullptr);
}
