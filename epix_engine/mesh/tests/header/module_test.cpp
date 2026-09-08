#include <gtest/gtest.h>

#include <epix/mesh.hpp>
#include <limits>
#include <unordered_set>
#include <webgpu/webgpu.hpp>

namespace mesh = epix::mesh;

TEST(MeshModule, BuiltInVertexAttributeIdsMatchBevy) {
    EXPECT_EQ(mesh::Mesh::ATTRIBUTE_POSITION.id.value, 0u);
    EXPECT_EQ(mesh::Mesh::ATTRIBUTE_NORMAL.id.value, 1u);
    EXPECT_EQ(mesh::Mesh::ATTRIBUTE_UV_0.id.value, 2u);
    EXPECT_EQ(mesh::Mesh::ATTRIBUTE_UV_1.id.value, 3u);
    EXPECT_EQ(mesh::Mesh::ATTRIBUTE_TANGENT.id.value, 4u);
    EXPECT_EQ(mesh::Mesh::ATTRIBUTE_COLOR.id.value, 5u);
    EXPECT_EQ(mesh::Mesh::ATTRIBUTE_JOINT_WEIGHT.id.value, 6u);
    EXPECT_EQ(mesh::Mesh::ATTRIBUTE_JOINT_INDEX.id.value, 7u);
    EXPECT_EQ(mesh::Mesh::FIRST_AVAILABLE_CUSTOM_ATTRIBUTE, 8u);
}

TEST(MeshModule, BaseMeshPipelineKeyEncodesPrimitiveTopologyInHighBits) {
    constexpr auto line = mesh::BaseMeshPipelineKey::from_primitive_topology(wgpu::PrimitiveTopology::eLineList);
    static_assert(line.primitive_topology() == wgpu::PrimitiveTopology::eLineList);
    EXPECT_EQ(line.bits() >> mesh::BaseMeshPipelineKey::PRIMITIVE_TOPOLOGY_SHIFT_BITS,
              static_cast<std::uint64_t>(wgpu::PrimitiveTopology::eLineList));
    EXPECT_TRUE((line | mesh::BaseMeshPipelineKey::MORPH_TARGETS).contains(line));
}

TEST(MeshModule, MeshUsageAndCloneMatchBevyContract) {
    constexpr auto both_worlds = static_cast<epix::assets::RenderAssetUsages>(
        epix::assets::RenderAssetUsages::MAIN_WORLD | epix::assets::RenderAssetUsages::RENDER_WORLD);
    auto source = mesh::make_box2d(20.0f, 10.0f);
    EXPECT_EQ(source.asset_usage, both_worlds);

    mesh::Mesh clone(source);
    EXPECT_EQ(clone.asset_usage, source.asset_usage);
    EXPECT_EQ(clone.count_vertices(), source.count_vertices());
    ASSERT_TRUE(clone.remove_attribute(mesh::Mesh::ATTRIBUTE_POSITION).has_value());
    EXPECT_FALSE(clone.contains_attribute(mesh::Mesh::ATTRIBUTE_POSITION));
    EXPECT_TRUE(source.contains_attribute(mesh::Mesh::ATTRIBUTE_POSITION));
}

TEST(MeshModule, RejectsIncompatibleAttributeType) {
    mesh::Mesh value(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::RENDER_WORLD);
    EXPECT_THROW(value.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, std::array{glm::vec2(0.0f, 0.0f)}),
                 std::invalid_argument);
}

TEST(MeshModule, Box2dBuildsIndexedQuad) {
    auto value = mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f));
    EXPECT_EQ(value.count_vertices(), 4);
    ASSERT_TRUE(value.indices().has_value());
    EXPECT_EQ(value.indices()->get().size(), 6);
    EXPECT_TRUE(value.contains_attribute(mesh::Mesh::ATTRIBUTE_COLOR));
}

TEST(MeshModule, CircleBuildsTriangleList) {
    auto value = mesh::make_circle(12.0f, std::nullopt, 16);
    EXPECT_EQ(value.get_primitive_type(), wgpu::PrimitiveTopology::eTriangleList);
    EXPECT_EQ(value.count_vertices(), 17);
    ASSERT_TRUE(value.indices().has_value());
    EXPECT_EQ(value.indices()->get().size(), 48);
}

TEST(MeshModule, Box2dUvBuildsTexturedQuad) {
    auto value = mesh::make_box2d_uv(20.0f, 10.0f, glm::vec4(0.25f, 0.5f, 0.75f, 1.0f), glm::vec4(0.5f));
    EXPECT_EQ(value.count_vertices(), 4);
    EXPECT_TRUE(value.contains_attribute(mesh::Mesh::ATTRIBUTE_UV_0));
    auto attribute = value.attribute(mesh::Mesh::ATTRIBUTE_UV_0);
    ASSERT_TRUE(attribute.has_value());
    const auto uvs = attribute->get().cspan_as<glm::vec2>();
    ASSERT_EQ(uvs.size(), 4u);
    EXPECT_EQ(uvs[0], glm::vec2(0.25f, 0.5f));
    EXPECT_EQ(uvs[2], glm::vec2(0.75f, 1.0f));
}

TEST(MeshModule, GetMeshVertexBufferLayoutMatchesBevy) {
    auto source = mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f));
    mesh::MeshVertexBufferLayouts layouts;
    const auto first = source.get_mesh_vertex_buffer_layout(layouts);
    const auto again = source.get_mesh_vertex_buffer_layout(layouts);
    ASSERT_TRUE(first.value);
    EXPECT_EQ(first, again);
    EXPECT_EQ(first.value->layout().array_stride, 28u);

    const auto remapped = first.value->get_layout(std::array{
        mesh::VertexAttributeDescriptor{
            .shader_location = 0, .id = mesh::Mesh::ATTRIBUTE_COLOR.id, .name = "color"},
        mesh::VertexAttributeDescriptor{
            .shader_location = 1, .id = mesh::Mesh::ATTRIBUTE_POSITION.id, .name = "position"},
    });
    ASSERT_TRUE(remapped.has_value());
    ASSERT_EQ(remapped->attributes.size(), 2u);
    EXPECT_EQ(remapped->attributes[0].shader_location, 0u);
    EXPECT_EQ(remapped->attributes[1].shader_location, 1u);
}

TEST(MeshModule, EmptyMeshDataExtractsOnceAndPreservesAccessState) {
    mesh::Mesh source(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::RENDER_WORLD);
    const auto missing_indices = source.try_indices();
    ASSERT_FALSE(missing_indices.has_value());
    EXPECT_EQ(missing_indices.error(), mesh::MeshAccessError::NotFound);
    EXPECT_TRUE(source.take_gpu_data().has_value());
    EXPECT_EQ(source.try_attributes().error(), mesh::MeshAccessError::ExtractedToRenderWorld);
    EXPECT_FALSE(source.take_gpu_data().has_value());
}
