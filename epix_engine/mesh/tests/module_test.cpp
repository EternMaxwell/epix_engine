#include <gtest/gtest.h>

import epix.mesh;
import glm;

namespace mesh = epix::mesh;

TEST(MeshModule, RejectsIncompatibleAttributeType) {
    mesh::Mesh mesh;
    auto result = mesh.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, std::array{glm::vec2(0.0f, 0.0f)});
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), mesh::MeshError::TypeIncompatible);
}

TEST(MeshModule, Box2dBuildsIndexedQuad) {
    auto mesh = mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f));
    EXPECT_EQ(mesh.count_vertices(), 4);
    ASSERT_TRUE(mesh.get_indices().has_value());
    EXPECT_EQ(mesh.get_indices()->get().size(), 6);
    EXPECT_TRUE(mesh.contains_attribute(mesh::Mesh::ATTRIBUTE_COLOR));
}

TEST(MeshModule, CircleBuildsTriangleList) {
    auto mesh = mesh::make_circle(12.0f, std::nullopt, 16);
    EXPECT_EQ(mesh.get_primitive_type(), wgpu::PrimitiveTopology::eTriangleList);
    EXPECT_EQ(mesh.count_vertices(), 17);
    ASSERT_TRUE(mesh.get_indices().has_value());
    EXPECT_EQ(mesh.get_indices()->get().size(), 48);
}

TEST(MeshModule, Box2dUvBuildsTexturedQuad) {
    auto mesh = mesh::make_box2d_uv(20.0f, 10.0f, glm::vec4(0.25f, 0.5f, 0.75f, 1.0f), glm::vec4(0.5f));

    EXPECT_EQ(mesh.count_vertices(), 4);
    EXPECT_TRUE(mesh.contains_attribute(mesh::Mesh::ATTRIBUTE_UV0));
    EXPECT_TRUE(mesh.contains_attribute(mesh::Mesh::ATTRIBUTE_COLOR));

    auto uv_attribute = mesh.get_attribute(mesh::Mesh::ATTRIBUTE_UV0);
    ASSERT_TRUE(uv_attribute.has_value());

    auto uvs = uv_attribute->get().data.cspan_as<glm::vec2>();
    ASSERT_EQ(uvs.size(), 4);
    EXPECT_EQ(uvs[0], glm::vec2(0.25f, 0.5f));
    EXPECT_EQ(uvs[2], glm::vec2(0.75f, 1.0f));
}