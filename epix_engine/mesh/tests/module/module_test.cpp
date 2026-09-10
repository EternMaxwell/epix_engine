#include <gtest/gtest.h>

#include <cstring>

import epix.mesh;
import epix.render;
import webgpu;
import glm;

namespace mesh = epix::mesh;

TEST(MeshErrors, WindingAndTriangleErrorsPreserveBevyVariantsAndMessages) {
    const mesh::MeshWindingInvertError wrong_winding{mesh::mesh_winding_invert_error::WrongTopology{}};
    EXPECT_TRUE(std::holds_alternative<mesh::mesh_winding_invert_error::WrongTopology>(wrong_winding));
    EXPECT_EQ(wrong_winding.to_string(),
              "Mesh winding inversion does not work for primitive topology `PointList`");

    const mesh::MeshWindingInvertError abrupt{mesh::mesh_winding_invert_error::AbruptIndicesEnd{}};
    EXPECT_TRUE(std::holds_alternative<mesh::mesh_winding_invert_error::AbruptIndicesEnd>(abrupt));
    EXPECT_EQ(abrupt.to_string(), "Indices weren't in chunks according to topology");

    const mesh::MeshWindingInvertError winding_access{mesh::MeshAccessError::ExtractedToRenderWorld};
    EXPECT_TRUE(std::holds_alternative<mesh::MeshAccessError>(winding_access));
    EXPECT_EQ(winding_access.to_string(),
              "Mesh access error: The mesh vertex/index data has been extracted to the RenderWorld (via "
              "`Mesh::asset_usage`)");

    const mesh::MeshTrianglesError positions{mesh::mesh_triangles_error::PositionsFormat{}};
    EXPECT_TRUE(std::holds_alternative<mesh::mesh_triangles_error::PositionsFormat>(positions));
    EXPECT_EQ(positions.to_string(), "Source mesh position data is not Float32x3");

    const mesh::MeshTrianglesError bad_indices{mesh::mesh_triangles_error::BadIndices{}};
    EXPECT_TRUE(std::holds_alternative<mesh::mesh_triangles_error::BadIndices>(bad_indices));
    EXPECT_EQ(bad_indices.to_string(), "Face index data references vertices that do not exist");

    const mesh::MeshTrianglesError triangle_access{mesh::MeshAccessError::NotFound};
    EXPECT_TRUE(std::holds_alternative<mesh::MeshAccessError>(triangle_access));
    EXPECT_EQ(triangle_access.to_string(), "mesh access error: The requested mesh data wasn't found in this mesh");
}

TEST(Indices, PushAndExtendPromoteU16StorageWithoutLosingValues) {
    mesh::Indices indices{std::vector<std::uint16_t>{}};
    static_assert(std::ranges::view<decltype(std::declval<const mesh::Indices&>().iter())>);

    indices.push(10);
    EXPECT_EQ(static_cast<wgpu::IndexFormat>(indices), wgpu::IndexFormat::eUint16);
    EXPECT_EQ(std::ranges::to<std::vector<std::size_t>>(indices.iter()), (std::vector<std::size_t>{10}));

    indices.extend(std::array<std::uint32_t, 3>{11, 0x10012, 0x10013});
    EXPECT_EQ(static_cast<wgpu::IndexFormat>(indices), wgpu::IndexFormat::eUint32);
    EXPECT_EQ(std::ranges::to<std::vector<std::size_t>>(indices.iter()),
              (std::vector<std::size_t>{10, 11, 0x10012, 0x10013}));

    indices.push(20);
    EXPECT_EQ(indices.len(), 5u);
    EXPECT_FALSE(indices.is_empty());
    ASSERT_NE(indices.as_u32(), nullptr);
    EXPECT_EQ(*indices.as_u32(), (std::vector<std::uint32_t>{10, 11, 0x10012, 0x10013, 20}));
}

TEST(Indices, MeshExposesExactIndexBytes) {
    mesh::Mesh value(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::RENDER_WORLD);
    value.insert_indices(mesh::Indices{std::vector<std::uint16_t>{1, 0x203, 4}});
    const auto bytes = value.get_index_buffer_bytes();
    ASSERT_TRUE(bytes.has_value());
    ASSERT_EQ(bytes->size(), 3u * sizeof(std::uint16_t));
    std::uint16_t middle = 0;
    std::memcpy(&middle, bytes->data() + sizeof(std::uint16_t), sizeof(middle));
    EXPECT_EQ(middle, 0x203u);
}

TEST(MeshComponents, Mesh2dAndMesh3dMatchBevyNewtypeContracts) {
    const auto id = epix::assets::AssetId<mesh::Mesh>::invalid();
    const epix::assets::Handle<mesh::Mesh> handle{id};
    mesh::Mesh2d mesh_2d{handle};
    mesh::Mesh3d mesh_3d{handle};

    EXPECT_EQ(static_cast<epix::assets::AssetId<mesh::Mesh>>(mesh_2d), id);
    EXPECT_EQ(static_cast<epix::assets::AssetId<mesh::Mesh>>(mesh_3d), id);
    EXPECT_EQ(epix::assets::AsAssetId<mesh::Mesh2d>::as_asset_id(mesh_2d), id);
    EXPECT_EQ(epix::assets::AsAssetId<mesh::Mesh3d>::as_asset_id(mesh_3d), id);
    EXPECT_EQ((*mesh_2d).id(), id);
    EXPECT_EQ((*mesh_3d).id(), id);

    auto app             = epix::app::App::create();
    const auto entity_2d = app.world_mut().spawn(mesh_2d).id();
    const auto entity_3d = app.world_mut().spawn(mesh_3d).id();
    EXPECT_TRUE(app.world().entity(entity_2d).contains<epix::transform::Transform>());
    EXPECT_TRUE(app.world().entity(entity_3d).contains<epix::transform::Transform>());

    EXPECT_EQ(mesh::MeshTag{}.value, 0u);
    EXPECT_EQ(mesh::MeshTag{.value = 17}, mesh::MeshTag{.value = 17});
}

TEST(MeshPlugin, ModifiedMeshAssetMarksOnlyReferencingMesh3dChanged) {
    auto app          = epix::app::App::create();
    auto asset_plugin = epix::assets::AssetPlugin{};
    asset_plugin.mode = epix::assets::AssetServerMode::Unprocessed;
    app.add_plugins(std::move(asset_plugin));
    app.add_plugins(mesh::MeshPlugin{});

    auto first = app.world_mut().resource_mut<epix::assets::Assets<mesh::Mesh>>().emplace(mesh::make_box2d(2.0f, 2.0f));
    auto second =
        app.world_mut().resource_mut<epix::assets::Assets<mesh::Mesh>>().emplace(mesh::make_box2d(4.0f, 4.0f));
    app.world_mut().spawn(mesh::Mesh3d{first});
    app.world_mut().spawn(mesh::Mesh3d{second});

    std::size_t modified_count = 0;
    app.add_systems(epix::app::PostUpdate,
                    epix::ecs::into([&modified_count](
                                        epix::ecs::Query<epix::ecs::Item<const mesh::Mesh3d&>,
                                                         epix::ecs::Filter<epix::ecs::Modified<mesh::Mesh3d>>> meshes) {
                        modified_count += std::ranges::distance(meshes.iter());
                    }).after(mesh::mark_3d_meshes_as_changed_if_their_assets_changed));

    app.update();
    EXPECT_EQ(modified_count, 2u);
    app.update();
    EXPECT_EQ(modified_count, 2u);

    auto stored = app.world_mut().resource_mut<epix::assets::Assets<mesh::Mesh>>().get_mut(first.id());
    ASSERT_TRUE(stored.has_value());
    stored->get().insert_attribute(mesh::Mesh::ATTRIBUTE_COLOR, std::array{glm::vec4(1.0f)});
    app.update();
    EXPECT_EQ(modified_count, 3u);
}

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
}

TEST(MeshModule, RejectsIncompatibleAttributeType) {
    mesh::Mesh mesh(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::RENDER_WORLD);
    EXPECT_THROW(mesh.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, std::array{glm::vec2(0.0f, 0.0f)}),
                 std::invalid_argument);
}

TEST(MeshModule, Box2dBuildsIndexedQuad) {
    auto mesh = mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f));
    EXPECT_EQ(mesh.count_vertices(), 4);
    ASSERT_TRUE(mesh.indices().has_value());
    EXPECT_EQ(mesh.indices()->get().len(), 6);
    EXPECT_TRUE(mesh.contains_attribute(mesh::Mesh::ATTRIBUTE_COLOR));
    EXPECT_EQ(mesh.asset_usage,
              static_cast<epix::assets::RenderAssetUsages>(epix::assets::RenderAssetUsages::MAIN_WORLD |
                                                           epix::assets::RenderAssetUsages::RENDER_WORLD));
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
    EXPECT_EQ(mesh.indices()->get().len(), 48);
}

TEST(MeshModule, Box2dUvBuildsTexturedQuad) {
    auto mesh = mesh::make_box2d_uv(20.0f, 10.0f, glm::vec4(0.25f, 0.5f, 0.75f, 1.0f), glm::vec4(0.5f));

    EXPECT_EQ(mesh.count_vertices(), 4);
    EXPECT_TRUE(mesh.contains_attribute(mesh::Mesh::ATTRIBUTE_UV_0));
    EXPECT_TRUE(mesh.contains_attribute(mesh::Mesh::ATTRIBUTE_COLOR));

    auto uv_attribute = mesh.attribute(mesh::Mesh::ATTRIBUTE_UV_0);
    ASSERT_TRUE(uv_attribute.has_value());

    auto uvs = uv_attribute->get().cspan_as<glm::vec2>();
    ASSERT_EQ(uvs.size(), 4);
    EXPECT_EQ(uvs[0], glm::vec2(0.25f, 0.5f));
    EXPECT_EQ(uvs[2], glm::vec2(0.75f, 1.0f));
}

// Bevy RenderAsset::byte_len for RenderMesh: sum of per-vertex attribute
// strides * vertex count + index bytes.
TEST(MeshModule, RenderAssetByteLenMatchesBevyContract) {
    mesh::Mesh mesh(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::RENDER_WORLD);
    mesh.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION,
                          std::array{glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 1.0f, 1.0f}});
    mesh.insert_attribute(mesh::Mesh::ATTRIBUTE_COLOR,
                          std::array{glm::vec4{1.0f, 1.0f, 1.0f, 1.0f}, glm::vec4{0.5f, 0.5f, 0.5f, 0.5f}});
    mesh.insert_indices(mesh::Indices{std::vector<std::uint16_t>{0, 1, 0}});

    const epix::render::RenderAsset<mesh::Mesh> asset{};
    const auto len = asset.byte_len(mesh);
    ASSERT_TRUE(len.has_value());
    EXPECT_EQ(*len, 62u);
}

TEST(MeshModule, EmptyMeshDataExtractsOnceAndPreservesAccessState) {
    mesh::Mesh source(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::RENDER_WORLD);
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
