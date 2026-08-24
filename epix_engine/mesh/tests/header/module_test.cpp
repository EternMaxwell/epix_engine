#include <gtest/gtest.h>

#include <epix/mesh.hpp>
#include <webgpu/webgpu.hpp>

namespace mesh = epix::mesh;

TEST(MeshModule, RejectsIncompatibleAttributeType) {
    mesh::Mesh mesh;
    auto result = mesh.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, std::array{glm::vec2(0.0f, 0.0f)});
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), mesh::MeshError::TypeIncompatible);
}

TEST(MeshModule, ComputesAabbFromPositionAttribute) {
    mesh::Mesh mesh;
    ASSERT_TRUE(mesh.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION,
                                      std::array{glm::vec3{-2.0f, 1.0f, 3.0f}, glm::vec3{4.0f, 5.0f, -1.0f}}));
    static_assert(epix::camera::MeshAabb<mesh::Mesh>);
    const auto aabb = mesh.compute_aabb();
    ASSERT_TRUE(aabb.has_value());
    EXPECT_EQ(aabb->min(), glm::vec3(-2.0f, 1.0f, -1.0f));
    EXPECT_EQ(aabb->max(), glm::vec3(4.0f, 5.0f, 3.0f));
}
