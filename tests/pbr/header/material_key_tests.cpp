#include <gtest/gtest.h>

#include <epix/pbr/material.hpp>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {
namespace pbr = epix::pbr;

struct KeyOperations {
    int copies     = 0;
    int hashes     = 0;
    bool fail_copy = false;
};

struct TrackedKey {
    int value;
    std::shared_ptr<KeyOperations> operations;

    TrackedKey(int value, std::shared_ptr<KeyOperations> operations)
        : value(value), operations(std::move(operations)) {}
    TrackedKey(const TrackedKey& other) : value(other.value), operations(other.operations) {
        ++operations->copies;
        if (operations->fail_copy) throw std::runtime_error("key clone failed");
    }
    TrackedKey(TrackedKey&&) noexcept = default;
    bool operator==(const TrackedKey& other) const { return value == other.value; }
};

struct KeyMaterial {};
}  // namespace

template <>
struct std::hash<TrackedKey> {
    std::size_t operator()(const TrackedKey& key) const {
        ++key.operations->hashes;
        return std::hash<int>{}(key.value);
    }
};

namespace {
std::size_t combine(std::size_t first, std::size_t second) {
    return first ^ (second + 0x9e3779b9 + (first << 6) + (first >> 2));
}

TEST(MaterialKey, CopyConstructionClonesThePayloadWithoutRehashing) {
    const auto operations = std::make_shared<KeyOperations>();
    const pbr::ErasedMaterialKey original{TrackedKey{17, operations}};
    operations->copies = operations->hashes = 0;

    const pbr::ErasedMaterialKey copy{original};
    EXPECT_EQ(operations->copies, 1);
    EXPECT_EQ(operations->hashes, 0);
    EXPECT_EQ(copy, original);
    EXPECT_EQ(copy.hash(), original.hash());
    EXPECT_EQ(copy.type_id(), original.type_id());
    EXPECT_EQ(copy.to_key<TrackedKey>().value, 17);
    EXPECT_EQ(operations->copies, 2) << "typed recovery is a separate clone";
}

TEST(MaterialKey, CopyAssignmentClonesAndReplacesThePreviousPayload) {
    const auto operations = std::make_shared<KeyOperations>();
    const pbr::ErasedMaterialKey original{TrackedKey{23, operations}};
    pbr::ErasedMaterialKey destination{std::string{"old"}};
    operations->copies = operations->hashes = 0;

    destination = original;
    EXPECT_EQ(operations->copies, 1);
    EXPECT_EQ(operations->hashes, 0);
    EXPECT_EQ(destination, original);
    EXPECT_EQ(destination.hash(), original.hash());
    EXPECT_EQ(destination.type_id(), original.type_id());
    EXPECT_EQ(destination.to_key<TrackedKey>().value, 23);
    EXPECT_THROW(destination.to_key<std::string>(), std::bad_cast);
}

TEST(MaterialKey, FailedClonesPropagateAndLeaveAnAssignmentTargetUnchanged) {
    const auto operations = std::make_shared<KeyOperations>();
    const pbr::ErasedMaterialKey original{TrackedKey{31, operations}};
    pbr::ErasedMaterialKey destination{std::string{"retained"}};
    const auto original_destination_hash = destination.hash();
    const auto original_destination_type = destination.type_id();
    operations->copies                   = 0;
    operations->fail_copy                = true;

    EXPECT_THROW({ const pbr::ErasedMaterialKey copy{original}; }, std::runtime_error);
    EXPECT_THROW(destination = original, std::runtime_error);
    EXPECT_EQ(operations->copies, 2);
    ASSERT_EQ(destination.type_id(), original_destination_type);
    EXPECT_EQ(destination.hash(), original_destination_hash);
    EXPECT_EQ(destination.to_key<std::string>(), "retained");
}

TEST(MaterialKey, MovesTransferThePayloadWithoutCloning) {
    static_assert(std::is_nothrow_move_constructible_v<pbr::ErasedMaterialKey>);
    static_assert(std::is_nothrow_move_assignable_v<pbr::ErasedMaterialKey>);
    const auto operations = std::make_shared<KeyOperations>();
    pbr::ErasedMaterialKey original{TrackedKey{41, operations}};
    const auto original_hash = original.hash();
    operations->copies = operations->hashes = 0;

    pbr::ErasedMaterialKey moved{std::move(original)};
    pbr::ErasedMaterialKey assigned;
    assigned = std::move(moved);
    EXPECT_EQ(operations->copies, 0);
    EXPECT_EQ(operations->hashes, 0);
    EXPECT_EQ(assigned.hash(), original_hash);
    EXPECT_EQ(assigned.type_id(), epix::meta::type_id<TrackedKey>());
    EXPECT_EQ(assigned.to_key<TrackedKey>().value, 41);
}

TEST(MaterialKey, PipelineKeyCopiesCloneTheirErasedData) {
    const auto operations = std::make_shared<KeyOperations>();
    const pbr::ErasedMaterialPipelineKey original{
        .mesh_key     = pbr::MeshPipelineKey::LIGHTMAPPED,
        .material_key = pbr::ErasedMaterialKey{TrackedKey{53, operations}},
        .type_id      = epix::meta::type_id<KeyMaterial>(),
    };
    operations->copies = operations->hashes = 0;

    const auto copy = original;
    EXPECT_EQ(operations->copies, 1);
    EXPECT_EQ(operations->hashes, 0);
    EXPECT_EQ(copy, original);
    EXPECT_EQ(std::hash<pbr::ErasedMaterialPipelineKey>{}(copy), std::hash<pbr::ErasedMaterialPipelineKey>{}(original));
}

TEST(MaterialKey, ErasedHashIncludesTheDataTypeAndCachedValueHash) {
    const auto operations = std::make_shared<KeyOperations>();
    const pbr::ErasedMaterialKey key{TrackedKey{61, operations}};
    operations->hashes = 0;
    // Rust hashes TypeId followed by the cached value hash. C++ composes those
    // same fields with the port's hash combiner, not Rust's platform hash bytes.
    const auto expected = combine(std::hash<epix::meta::type_index>{}(key.type_id()), key.hash());
    EXPECT_EQ(std::hash<pbr::ErasedMaterialKey>{}(key), expected);
    EXPECT_EQ(operations->hashes, 0);
}

TEST(MaterialKey, PipelineHashIncludesTheCompleteErasedDataIdentity) {
    const pbr::ErasedMaterialPipelineKey key{
        .mesh_key     = pbr::MeshPipelineKey::LIGHTMAPPED,
        .material_key = pbr::ErasedMaterialKey{71u},
        .type_id      = epix::meta::type_id<KeyMaterial>(),
    };
    const auto material_hash =
        combine(std::hash<epix::meta::type_index>{}(key.material_key.type_id()), key.material_key.hash());
    const auto expected = combine(combine(static_cast<std::size_t>(key.mesh_key.bits()), material_hash),
                                  std::hash<epix::meta::type_index>{}(key.type_id));
    EXPECT_EQ(std::hash<pbr::ErasedMaterialPipelineKey>{}(key), expected);
}
}  // namespace
