#include <gtest/gtest.h>

#include <epix/render/specialized_pipeline.hpp>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
namespace render = epix::render;
namespace mesh = epix::mesh;

struct KeyOperations {
    bool fail_equality = false;
    bool fail_hash = false;
};

struct UserKey {
    int value = 0;
    std::shared_ptr<KeyOperations> operations;

    bool operator==(const UserKey& other) const {
        if (operations->fail_equality) throw std::runtime_error("user equality failed");
        return value == other.value;
    }
};
}  // namespace

template <>
struct std::hash<UserKey> {
    std::size_t operator()(const UserKey& key) const {
        if (key.operations->fail_hash) throw std::runtime_error("user hash failed");
        return std::hash<int>{}(key.value);
    }
};

namespace {
template <typename K>
struct Pipeline {
    using Key = K;
    std::expected<render::RenderPipelineDescriptor, render::SpecializedMeshPipelineError>
    specialize(Key, const mesh::MeshVertexBufferLayoutRef&) const;
};

using UserCache = render::SpecializedMeshPipelines<Pipeline<UserKey>>;
using PlainCache = render::SpecializedMeshPipelines<Pipeline<int>>;

TEST(SpecializedKeyContract, EqualityAllowsTheUserKeyToThrow) {
    const auto operations = std::make_shared<KeyOperations>();
    const UserCache::MeshLayoutKey first{{}, UserKey{7, operations}};
    const UserCache::MeshLayoutKey second{{}, UserKey{7, operations}};
    ASSERT_FALSE(noexcept(first == second)) << "A wrapper must not turn a user exception into termination";
    operations->fail_equality = true;
    EXPECT_THROW((void)(first == second), std::runtime_error);
    operations->fail_equality = false;
    EXPECT_EQ(first, second);
}

TEST(SpecializedKeyContract, HashAllowsTheUserKeyToThrow) {
    const auto operations = std::make_shared<KeyOperations>();
    const UserCache::MeshLayoutKey key{{}, UserKey{11, operations}};
    const UserCache::MeshLayoutKeyHash hash;
    ASSERT_FALSE(noexcept(hash(key)));
    const auto expected = hash(key);
    operations->fail_hash = true;
    EXPECT_THROW((void)hash(key), std::runtime_error);
    operations->fail_hash = false;
    EXPECT_EQ(hash(key), expected);
}

TEST(SpecializedKeyContract, NonthrowingPrimitiveOperationsRemainNonthrowing) {
    const PlainCache::MeshLayoutKey first{{}, 7};
    const PlainCache::MeshLayoutKey second{{}, 7};
    const PlainCache::MeshLayoutKeyHash hash;
    EXPECT_TRUE(noexcept(first == second));
    EXPECT_TRUE(noexcept(hash(first)));
    EXPECT_EQ(first, second);
    EXPECT_EQ(hash(first), hash(second));
}

TEST(SpecializedKeyContract, RealMeshLayoutCacheSurvivesAUserHashFailure) {
    const auto operations = std::make_shared<KeyOperations>();
    const UserCache::MeshLayoutKey key{{}, UserKey{13, operations}};
    ASSERT_FALSE(noexcept(UserCache::MeshLayoutKeyHash{}(key)));
    UserCache cache;
    cache.mesh_layout_cache.emplace(key, render::CachedPipelineId{});
    ASSERT_EQ(cache.mesh_layout_cache.size(), 1u);
    operations->fail_hash = true;
    EXPECT_THROW((void)cache.mesh_layout_cache.find(key), std::runtime_error);
    operations->fail_hash = false;
    EXPECT_NE(cache.mesh_layout_cache.find(key), cache.mesh_layout_cache.end());
    EXPECT_EQ(cache.mesh_layout_cache.size(), 1u);
}

TEST(SpecializedKeyContract, RealMeshLayoutCacheSurvivesAUserEqualityFailure) {
    const auto operations = std::make_shared<KeyOperations>();
    const UserCache::MeshLayoutKey key{{}, UserKey{17, operations}};
    ASSERT_FALSE(noexcept(key == key));
    UserCache cache;
    cache.mesh_layout_cache.emplace(key, render::CachedPipelineId{});
    operations->fail_equality = true;
    EXPECT_THROW((void)cache.mesh_layout_cache.find(key), std::runtime_error);
    operations->fail_equality = false;
    EXPECT_NE(cache.mesh_layout_cache.find(key), cache.mesh_layout_cache.end());
    EXPECT_EQ(cache.mesh_layout_cache.size(), 1u);
}
}  // namespace
