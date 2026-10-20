#include <gtest/gtest.h>

#include <epix/pbr/material.hpp>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
namespace pbr = epix::pbr;

struct KeyOperations {
    bool fail_equality = false;
    bool fail_hash = false;
    int hashes = 0;
    std::vector<std::pair<int, int>> comparisons;
};

struct UserKey {
    int value = 0;
    int identity = 0;
    std::shared_ptr<KeyOperations> operations;

    bool operator==(const UserKey& other) const {
        operations->comparisons.emplace_back(identity, other.identity);
        if (operations->fail_equality) throw std::runtime_error("material equality failed");
        return value == other.value;
    }
};

struct UserMaterial {};
}  // namespace

template <>
struct std::hash<UserKey> {
    std::size_t operator()(const UserKey& key) const {
        ++key.operations->hashes;
        if (key.operations->fail_hash) throw std::runtime_error("material hash failed");
        return std::hash<int>{}(key.value);
    }
};

template <>
struct epix::render::render_resource::AsBindGroup<UserMaterial> {
    using Data = UserKey;
};

namespace {
TEST(MaterialKeyContract, TypedEqualityAllowsTheAssociatedDataToThrow) {
    const auto operations = std::make_shared<KeyOperations>();
    const pbr::MaterialPipelineKey<UserMaterial> first{.bind_group_data = UserKey{7, 101, operations}};
    const pbr::MaterialPipelineKey<UserMaterial> second{.bind_group_data = UserKey{7, 202, operations}};
    ASSERT_FALSE(noexcept(first == second));
    operations->fail_equality = true;
    EXPECT_THROW((void)(first == second), std::runtime_error);
    operations->fail_equality = false;
    EXPECT_EQ(first, second);
}

TEST(MaterialKeyContract, ErasedEqualityAllowsTheUserComparisonToThrow) {
    const auto operations = std::make_shared<KeyOperations>();
    const pbr::ErasedMaterialKey first{UserKey{11, 101, operations}};
    const pbr::ErasedMaterialKey second{UserKey{11, 202, operations}};
    ASSERT_FALSE(noexcept(first == second));
    operations->fail_equality = true;
    EXPECT_THROW((void)(first == second), std::runtime_error);
    operations->fail_equality = false;
    EXPECT_EQ(first, second);
}

TEST(MaterialKeyContract, PipelineEqualityPropagatesAndShortCircuitsLikeItsMembers) {
    const auto operations = std::make_shared<KeyOperations>();
    const pbr::ErasedMaterialPipelineKey first{
        .material_key = pbr::ErasedMaterialKey{UserKey{13, 101, operations}},
        .type_id = epix::meta::type_id<UserMaterial>(),
    };
    auto second = first;
    ASSERT_FALSE(noexcept(first == second));
    second.mesh_key = pbr::MeshPipelineKey::LIGHTMAPPED;
    operations->fail_equality = true;
    EXPECT_FALSE(first == second);
    EXPECT_TRUE(operations->comparisons.empty());
    second.mesh_key = first.mesh_key;
    EXPECT_THROW((void)(first == second), std::runtime_error);
    operations->fail_equality = false;
    EXPECT_EQ(first, second);
}

TEST(MaterialKeyContract, ErasedEqualityPreservesLeftAndRightOperandOrder) {
    const auto operations = std::make_shared<KeyOperations>();
    const pbr::ErasedMaterialKey first{UserKey{17, 101, operations}};
    const pbr::ErasedMaterialKey second{UserKey{17, 202, operations}};
    // Equality is still a symmetric equivalence relation. The identities only
    // record dispatch order, not the value returned by the comparison.
    EXPECT_TRUE(first == second);
    ASSERT_EQ(operations->comparisons.size(), 1u);
    EXPECT_EQ(operations->comparisons[0], (std::pair{101, 202}));
    operations->comparisons.clear();
    EXPECT_TRUE(second == first);
    ASSERT_EQ(operations->comparisons.size(), 1u);
    EXPECT_EQ(operations->comparisons[0], (std::pair{202, 101}));
    const pbr::ErasedMaterialKey other_type{17};
    operations->comparisons.clear();
    EXPECT_FALSE(first == other_type);
    EXPECT_TRUE(operations->comparisons.empty());
}

TEST(MaterialKeyContract, CachedErasedHashesRemainNonthrowingWithoutRehashingUserData) {
    const auto operations = std::make_shared<KeyOperations>();
    const pbr::ErasedMaterialPipelineKey key{
        .material_key = pbr::ErasedMaterialKey{UserKey{19, 101, operations}},
        .type_id = epix::meta::type_id<UserMaterial>(),
    };
    const std::hash<pbr::ErasedMaterialKey> data_hash;
    const std::hash<pbr::ErasedMaterialPipelineKey> pipeline_hash;
    const auto expected_data = data_hash(key.material_key);
    const auto expected_pipeline = pipeline_hash(key);
    operations->hashes = 0;
    operations->fail_hash = true;
    EXPECT_TRUE(noexcept(data_hash(key.material_key)));
    EXPECT_TRUE(noexcept(pipeline_hash(key)));
    EXPECT_EQ(data_hash(key.material_key), expected_data);
    EXPECT_EQ(pipeline_hash(key), expected_pipeline);
    EXPECT_EQ(operations->hashes, 0);
}

TEST(MaterialKeyContract, ActualMeshLayoutCachePropagatesComparisonFailureAndRecovers) {
    const auto operations = std::make_shared<KeyOperations>();
    using Cache = epix::render::SpecializedMeshPipelines<pbr::MaterialPipelineSpecializer>;
    const Cache::MeshLayoutKey key{
        .key = pbr::ErasedMaterialPipelineKey{
            .material_key = pbr::ErasedMaterialKey{UserKey{23, 101, operations}},
            .type_id = epix::meta::type_id<UserMaterial>(),
        },
    };
    ASSERT_FALSE(noexcept(key == key));
    Cache cache;
    cache.mesh_layout_cache.emplace(key, epix::render::CachedPipelineId{37});
    operations->fail_equality = true;
    EXPECT_THROW((void)cache.mesh_layout_cache.find(key), std::runtime_error);
    operations->fail_equality = false;
    const auto found = cache.mesh_layout_cache.find(key);
    ASSERT_NE(found, cache.mesh_layout_cache.end());
    EXPECT_EQ(found->second.get(), 37u);
    EXPECT_EQ(cache.mesh_layout_cache.size(), 1u);
}
}  // namespace
