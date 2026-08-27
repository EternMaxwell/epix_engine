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

TEST(MeshModule, TransfersRenderWorldOnlyGpuDataOnce) {
    mesh::Mesh direct_source = mesh::make_box2d(24.0f, 12.0f);
    ASSERT_GT(direct_source.count_vertices(), 0u);
    EXPECT_TRUE(direct_source.take_gpu_data().has_value());

    mesh::Mesh source = mesh::make_box2d(24.0f, 12.0f);
    ASSERT_GT(source.count_vertices(), 0u);

    epix::render::RenderAsset<mesh::Mesh> render_asset;
    auto extracted = render_asset.take_gpu_data(source, nullptr);
    ASSERT_TRUE(extracted.has_value());
    EXPECT_GT(extracted->count_vertices(), 0u);
    EXPECT_EQ(source.count_vertices(), 0u);

    auto repeated = render_asset.take_gpu_data(source, nullptr);
    ASSERT_FALSE(repeated.has_value());
    EXPECT_EQ(repeated.error(), epix::render::AssetExtractionError::AlreadyExtracted);
}

TEST(MeshModule, ExtractSystemMovesRenderWorldOnlyMeshToRenderWorld) {
    struct ResourceIdShiftA {};
    struct ResourceIdShiftB {};
    struct ResourceIdShiftC {};

    epix::ecs::World main_world(7);
    epix::ecs::World render_world(7);
    render_world.insert_resource(epix::app::ExtractedWorld{main_world});
    render_world.insert_resource(epix::render::ExtractedAssets<mesh::Mesh>{});
    render_world.insert_resource(epix::render::RenderAssets<mesh::Mesh>{});

    // Keep the extracted resource ids distinct from render-world ids, as they
    // are in the production main/render-world split.
    main_world.insert_resource(ResourceIdShiftA{});
    main_world.insert_resource(ResourceIdShiftB{});
    main_world.insert_resource(ResourceIdShiftC{});
    main_world.insert_resource(epix::assets::Assets<mesh::Mesh>{});
    main_world.insert_resource(epix::ecs::Events<epix::assets::AssetEvent<mesh::Mesh>>{});

    auto handle = main_world.resource_mut<epix::assets::Assets<mesh::Mesh>>().emplace(mesh::make_box2d(24.0f, 12.0f));
    main_world.resource_mut<epix::ecs::Events<epix::assets::AssetEvent<mesh::Mesh>>>().push(
        epix::assets::AssetEvent<mesh::Mesh>::added(handle.id()));

    auto system = epix::ecs::make_system_unique(epix::render::extract_render_asset<mesh::Mesh>);
    system->initialize(render_world);
    ASSERT_TRUE(system->run({}, render_world).has_value());

    const auto& extracted = render_world.resource<epix::render::ExtractedAssets<mesh::Mesh>>();
    ASSERT_EQ(extracted.extracted.size(), 1u);
    EXPECT_EQ(extracted.extracted.front().first, handle.id());
    EXPECT_GT(extracted.extracted.front().second.count_vertices(), 0u);

    const auto source = main_world.resource<epix::assets::Assets<mesh::Mesh>>().get(handle.id());
    ASSERT_TRUE(source.has_value());
    EXPECT_EQ(source->get().count_vertices(), 0u);
}
