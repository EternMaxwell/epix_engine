#include <gtest/gtest.h>

import epix.mesh;
import webgpu;
import glm;

namespace mesh = epix::mesh;

TEST(MeshModule, RejectsIncompatibleAttributeType) {
    mesh::Mesh mesh(wgpu::PrimitiveTopology::eTriangleList, epix::render::RenderAssetUsages::RENDER_WORLD);
    EXPECT_THROW(mesh.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, std::array{glm::vec2(0.0f, 0.0f)}),
                 std::invalid_argument);
}

TEST(MeshModule, Box2dBuildsIndexedQuad) {
    auto mesh = mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f));
    EXPECT_EQ(mesh.count_vertices(), 4);
    ASSERT_TRUE(mesh.indices().has_value());
    EXPECT_EQ(mesh.indices()->get().size(), 6);
    EXPECT_TRUE(mesh.contains_attribute(mesh::Mesh::ATTRIBUTE_COLOR));
    EXPECT_EQ(mesh.asset_usage,
              static_cast<epix::render::RenderAssetUsages>(epix::render::RenderAssetUsages::MAIN_WORLD |
                                                           epix::render::RenderAssetUsages::RENDER_WORLD));
}

TEST(MeshModule, MeshCloneRetainsIndependentGpuDataAndUsage) {
    auto source = mesh::make_box2d(20.0f, 10.0f);
    mesh::Mesh clone(source);

    EXPECT_EQ(clone.asset_usage, source.asset_usage);
    EXPECT_EQ(clone.count_vertices(), source.count_vertices());
    ASSERT_TRUE(clone.remove_attribute(mesh::Mesh::ATTRIBUTE_POSITION).has_value());
    EXPECT_FALSE(clone.contains_attribute(mesh::Mesh::ATTRIBUTE_POSITION));
    EXPECT_TRUE(source.contains_attribute(mesh::Mesh::ATTRIBUTE_POSITION));
}

TEST(MeshModule, CircleBuildsTriangleList) {
    auto mesh = mesh::make_circle(12.0f, std::nullopt, 16);
    EXPECT_EQ(mesh.get_primitive_type(), wgpu::PrimitiveTopology::eTriangleList);
    EXPECT_EQ(mesh.count_vertices(), 17);
    ASSERT_TRUE(mesh.indices().has_value());
    EXPECT_EQ(mesh.indices()->get().size(), 48);
}

TEST(MeshModule, Box2dUvBuildsTexturedQuad) {
    auto mesh = mesh::make_box2d_uv(20.0f, 10.0f, glm::vec4(0.25f, 0.5f, 0.75f, 1.0f), glm::vec4(0.5f));

    EXPECT_EQ(mesh.count_vertices(), 4);
    EXPECT_TRUE(mesh.contains_attribute(mesh::Mesh::ATTRIBUTE_UV0));
    EXPECT_TRUE(mesh.contains_attribute(mesh::Mesh::ATTRIBUTE_COLOR));

    auto uv_attribute = mesh.attribute(mesh::Mesh::ATTRIBUTE_UV0);
    ASSERT_TRUE(uv_attribute.has_value());

    auto uvs = uv_attribute->get().cspan_as<glm::vec2>();
    ASSERT_EQ(uvs.size(), 4);
    EXPECT_EQ(uvs[0], glm::vec2(0.25f, 0.5f));
    EXPECT_EQ(uvs[2], glm::vec2(0.75f, 1.0f));
}

// Bevy RenderAsset::byte_len for RenderMesh: sum of per-vertex attribute
// strides * vertex count + index bytes.
TEST(MeshModule, RenderAssetByteLenMatchesBevyContract) {
    mesh::Mesh mesh(wgpu::PrimitiveTopology::eTriangleList, epix::render::RenderAssetUsages::RENDER_WORLD);
    mesh.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION,
                          std::array{glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 1.0f, 1.0f}});
    mesh.insert_attribute(mesh::Mesh::ATTRIBUTE_COLOR,
                          std::array{glm::vec4{1.0f, 1.0f, 1.0f, 1.0f}, glm::vec4{0.5f, 0.5f, 0.5f, 0.5f}});
    mesh.insert_indices<std::uint16_t>(std::array<std::uint16_t, 3>{0, 1, 0});

    const epix::render::RenderAsset<mesh::Mesh> asset{};
    const auto len = asset.byte_len(mesh);
    ASSERT_TRUE(len.has_value());
    EXPECT_EQ(*len, 62u);
}

TEST(MeshModule, EmptyMeshDataExtractsOnceAndPreservesAccessState) {
    mesh::Mesh source(wgpu::PrimitiveTopology::eTriangleList, epix::render::RenderAssetUsages::RENDER_WORLD);
    const auto missing_indices = source.try_indices();
    ASSERT_FALSE(missing_indices.has_value());
    EXPECT_EQ(missing_indices.error(), mesh::MeshAccessError::NotFound);

    auto extracted = source.take_gpu_data();
    ASSERT_TRUE(extracted.has_value());
    EXPECT_EQ(extracted->count_vertices(), 0u);

    const auto source_attributes = source.try_attributes();
    ASSERT_FALSE(source_attributes.has_value());
    EXPECT_EQ(source_attributes.error(), mesh::MeshAccessError::ExtractedToRenderWorld);
    auto repeated = source.take_gpu_data();
    ASSERT_FALSE(repeated.has_value());
    EXPECT_EQ(repeated.error(), mesh::MeshAccessError::ExtractedToRenderWorld);
}
