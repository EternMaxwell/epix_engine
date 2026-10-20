#include <gtest/gtest.h>

#include <epix/sprite_render/material2d.hpp>
#include <functional>
#include <memory>
#include <stdexcept>

namespace {
namespace sprite_render = epix::sprite_render;
namespace render = epix::render;

struct KeyOperations {
    bool fail_equality = false;
    bool fail_hash = false;
};

struct UserKey {
    int value = 0;
    std::shared_ptr<KeyOperations> operations;
    bool operator==(const UserKey& other) const {
        if (operations->fail_equality) throw std::runtime_error("2d equality failed");
        return value == other.value;
    }
};
struct UserMaterial {};
struct PlainMaterial {};
}  // namespace

template <>
struct std::hash<UserKey> {
    std::size_t operator()(const UserKey& key) const {
        if (key.operations->fail_hash) throw std::runtime_error("2d hash failed");
        return std::hash<int>{}(key.value);
    }
};
template <>
struct epix::render::render_resource::AsBindGroup<UserMaterial> {
    using Data = UserKey;
};
template <>
struct epix::render::render_resource::AsBindGroup<PlainMaterial> {
    using Data = std::monostate;
};

namespace {
using Key = sprite_render::Material2dKey<UserMaterial>;
using Cache = render::SpecializedMeshPipelines<sprite_render::Material2dPipeline<UserMaterial>>;

TEST(Material2dKeyContract, EqualityAllowsAssociatedDataToThrow) {
    const auto operations = std::make_shared<KeyOperations>();
    const Key first{.bind_group_data = UserKey{7, operations}};
    const Key second{.bind_group_data = UserKey{7, operations}};
    ASSERT_FALSE(noexcept(first == second));
    operations->fail_equality = true;
    EXPECT_THROW((void)(first == second), std::runtime_error);
    operations->fail_equality = false;
    EXPECT_EQ(first, second);
}

TEST(Material2dKeyContract, HashAllowsAssociatedDataToThrow) {
    const auto operations = std::make_shared<KeyOperations>();
    const Key key{.bind_group_data = UserKey{11, operations}};
    const std::hash<Key> hash;
    ASSERT_FALSE(noexcept(hash(key)));
    const auto expected = hash(key);
    operations->fail_hash = true;
    EXPECT_THROW((void)hash(key), std::runtime_error);
    operations->fail_hash = false;
    EXPECT_EQ(hash(key), expected);
}

TEST(Material2dKeyContract, UnitDataOperationsRemainNonthrowing) {
    using PlainKey = sprite_render::Material2dKey<PlainMaterial>;
    const PlainKey first;
    const PlainKey second;
    EXPECT_TRUE(noexcept(first == second));
    EXPECT_TRUE(noexcept(std::hash<PlainKey>{}(first)));
    EXPECT_EQ(first, second);
    EXPECT_EQ(std::hash<PlainKey>{}(first), std::hash<PlainKey>{}(second));
}

TEST(Material2dKeyContract, ActualPipelineCacheSurvivesUserHashFailure) {
    const auto operations = std::make_shared<KeyOperations>();
    const Cache::MeshLayoutKey key{.key = Key{.bind_group_data = UserKey{13, operations}}};
    ASSERT_FALSE(noexcept(Cache::MeshLayoutKeyHash{}(key)));
    Cache cache;
    cache.mesh_layout_cache.emplace(key, render::CachedPipelineId{41});
    operations->fail_hash = true;
    EXPECT_THROW((void)cache.mesh_layout_cache.find(key), std::runtime_error);
    operations->fail_hash = false;
    const auto found = cache.mesh_layout_cache.find(key);
    ASSERT_NE(found, cache.mesh_layout_cache.end());
    EXPECT_EQ(found->second.get(), 41u);
    EXPECT_EQ(cache.mesh_layout_cache.size(), 1u);
}

TEST(Material2dKeyContract, ActualPipelineCacheSurvivesUserEqualityFailure) {
    const auto operations = std::make_shared<KeyOperations>();
    const Cache::MeshLayoutKey key{.key = Key{.bind_group_data = UserKey{17, operations}}};
    ASSERT_FALSE(noexcept(key == key));
    Cache cache;
    cache.mesh_layout_cache.emplace(key, render::CachedPipelineId{43});
    operations->fail_equality = true;
    EXPECT_THROW((void)cache.mesh_layout_cache.find(key), std::runtime_error);
    operations->fail_equality = false;
    const auto found = cache.mesh_layout_cache.find(key);
    ASSERT_NE(found, cache.mesh_layout_cache.end());
    EXPECT_EQ(found->second.get(), 43u);
    EXPECT_EQ(cache.mesh_layout_cache.size(), 1u);
}
}  // namespace
