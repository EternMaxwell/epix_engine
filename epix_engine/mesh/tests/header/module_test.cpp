#include <gtest/gtest.h>

#include <epix/mesh.hpp>
#include <webgpu/webgpu.hpp>

namespace mesh = epix::mesh;

TEST(MeshModule, UsesBevyShapedAlphaMode2dVariants) {
    static_assert(std::same_as<mesh::MeshAlphaMode2d,
                               std::variant<mesh::MeshAlphaMode2dOpaque,
                                            mesh::MeshAlphaMode2dMask,
                                            mesh::MeshAlphaMode2dBlend>>);
    EXPECT_TRUE(std::holds_alternative<mesh::MeshAlphaMode2dOpaque>(mesh::MeshMaterial2d{}.alpha_mode));
    const mesh::MeshAlphaMode2d mask = mesh::MeshAlphaMode2dMask{.cutoff = 0.35f};
    EXPECT_EQ(std::get<mesh::MeshAlphaMode2dMask>(mask).cutoff, 0.35f);
}

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

// Bevy RenderAsset::byte_len for RenderMesh: sum of per-vertex attribute
// strides * vertex count + index bytes. Used by the render-asset byte limiter.
TEST(MeshModule, RenderAssetByteLenMatchesBevyContract) {
    mesh::Mesh mesh;
    ASSERT_TRUE(mesh.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION, std::array{
                                                                          glm::vec3{0.0f, 0.0f, 0.0f},
                                                                          glm::vec3{1.0f, 1.0f, 1.0f}}));
    ASSERT_TRUE(mesh.insert_attribute(mesh::Mesh::ATTRIBUTE_COLOR, std::array{
                                                                       glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
                                                                       glm::vec4{0.5f, 0.5f, 0.5f, 0.5f}}));
    mesh.insert_indices<std::uint16_t>(std::array<std::uint16_t, 3>{0, 1, 0});

    const epix::render::RenderAsset<mesh::Mesh> asset{};
    const auto len = asset.byte_len(mesh);
    ASSERT_TRUE(len.has_value());
    // (12 float3 + 16 float4) * 2 vertices + (3 * 2-byte u16 indices) = 62.
    EXPECT_EQ(*len, 62u);
}

// Bevy 0.18 MeshAllocatorSettings defaults.
TEST(MeshModule, MeshAllocatorSettingsMatchBevyDefaults) {
    const mesh::MeshAllocatorSettings settings;
    EXPECT_EQ(settings.min_slab_size, 1024ull * 1024);
    EXPECT_EQ(settings.max_slab_size, 1024ull * 1024 * 512);
    EXPECT_EQ(settings.large_threshold, 1024ull * 1024 * 256);
    EXPECT_EQ(settings.growth_factor, 1.5);
}

// Bevy SlabId / MeshBufferSlice are value types; verify identity + element range.
TEST(MeshModule, SlabIdAndMeshBufferSliceTypes) {
    const mesh::SlabId zero;
    const mesh::SlabId id{42};
    EXPECT_EQ(zero, mesh::SlabId{});
    EXPECT_EQ(id, mesh::SlabId{42});
    EXPECT_NE(id, mesh::SlabId{43});
    EXPECT_EQ(id.value, 42u);

    const mesh::MeshBufferSlice slice{nullptr, 3u, 9u};
    EXPECT_EQ(slice.buffer, nullptr);
    EXPECT_EQ(slice.begin, 3u);
    EXPECT_EQ(slice.end, 9u);
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
