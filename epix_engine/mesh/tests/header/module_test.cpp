#include <gtest/gtest.h>

#include <epix/mesh.hpp>
#include <limits>
#include <unordered_set>
#include <webgpu/webgpu.hpp>

namespace mesh = epix::mesh;

TEST(MeshComponents, Mesh2dAndMesh3dMatchBevyNewtypeContracts) {
    const auto id = epix::assets::AssetId<mesh::Mesh>::invalid();
    const epix::assets::Handle<mesh::Mesh> handle{id};
    mesh::Mesh2d mesh_2d{handle};
    mesh::Mesh3d mesh_3d{handle};

    EXPECT_EQ(mesh_2d.handle, handle);
    EXPECT_EQ(mesh_3d.handle, handle);
    EXPECT_EQ(static_cast<epix::assets::AssetId<mesh::Mesh>>(mesh_2d), id);
    EXPECT_EQ(static_cast<epix::assets::AssetId<mesh::Mesh>>(mesh_3d), id);
    EXPECT_EQ(epix::assets::AsAssetId<mesh::Mesh2d>::as_asset_id(mesh_2d), id);
    EXPECT_EQ(epix::assets::AsAssetId<mesh::Mesh3d>::as_asset_id(mesh_3d), id);
    EXPECT_EQ((*mesh_2d).id(), id);
    EXPECT_EQ((*mesh_3d).id(), id);
    EXPECT_EQ(mesh::Mesh2d{}.handle.id(), epix::assets::AssetId<mesh::Mesh>::invalid());
    EXPECT_EQ(mesh::Mesh3d{}.handle.id(), epix::assets::AssetId<mesh::Mesh>::invalid());

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
        mesh::VertexAttributeDescriptor{.shader_location = 0, .id = mesh::Mesh::ATTRIBUTE_COLOR.id, .name = "color"},
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
