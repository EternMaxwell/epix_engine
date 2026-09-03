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

// Empty MeshAllocator exposes Bevy's query surface with no allocations.
TEST(MeshModule, MeshAllocatorEmptyQuerySurface) {
    const mesh::MeshAllocator allocator;
    EXPECT_EQ(allocator.slab_count(), 0u);
    EXPECT_EQ(allocator.slabs_size(), 0u);
    EXPECT_EQ(allocator.allocations(), 0u);
}

// MeshAllocator::allocate creates/reuses a general slab by layout.
TEST(MeshModule, MeshAllocatorAllocatePacksByLayout) {
    const mesh::MeshAllocatorSettings s;
    const auto v12 = mesh::ElementLayout::make(mesh::ElementClass::Vertex, 12);
    mesh::MeshAllocator allocator;
    auto a = allocator.allocate(2, v12);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->first.value, 0u);
    EXPECT_EQ(a->second.offset, 0u);
    EXPECT_EQ(allocator.slab_count(), 1u);
    EXPECT_GT(allocator.slabs_size(), 0u);
    // Same layout packs into the existing slab at the next slot.
    auto b = allocator.allocate(3, v12);
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->first, a->first);
    EXPECT_EQ(b->second.offset, 2u);
    EXPECT_EQ(allocator.slab_count(), 1u);
    // Different layout -> a new slab.
    const auto i16 = mesh::ElementLayout::make(mesh::ElementClass::Index, 2);
    auto c         = allocator.allocate(1, i16);
    ASSERT_TRUE(c.has_value());
    EXPECT_NE(c->first, a->first);
    EXPECT_EQ(allocator.slab_count(), 2u);
}

// ElementLayout slot math (Bevy: slot size is a multiple of both the element
// size and the 4-byte copy-buffer alignment).
TEST(MeshModule, ElementLayoutSlotMath) {
    // stride 12 (float3 vertex) -> 1 element/slot, 12-byte slot.
    const auto v = mesh::ElementLayout::make(mesh::ElementClass::Vertex, 12);
    EXPECT_EQ(v.elements_per_slot, 1u);
    EXPECT_EQ(v.slot_size(), 12u);
    // stride 2 (u16 index) -> 2 elements/slot (slot 4, divisible by 4).
    const auto i16 = mesh::ElementLayout::make(mesh::ElementClass::Index, 2);
    EXPECT_EQ(i16.elements_per_slot, 2u);
    EXPECT_EQ(i16.slot_size(), 4u);
    // stride 4 -> 1 element/slot, 4-byte slot.
    EXPECT_EQ(mesh::ElementLayout::make(mesh::ElementClass::Index, 4).slot_size(), 4u);
    // stride 6 -> 2 elements/slot (slot 12, divisible by 4).
    const auto v6 = mesh::ElementLayout::make(mesh::ElementClass::Vertex, 6);
    EXPECT_EQ(v6.elements_per_slot, 2u);
    EXPECT_EQ(v6.slot_size(), 12u);
    // stride 1 (u8) -> 4 elements/slot (slot 4).
    EXPECT_EQ(mesh::ElementLayout::make(mesh::ElementClass::Vertex, 1).slot_size(), 4u);
}

// compute_grow_capacity mirrors Bevy GeneralSlab::grow_if_necessary.
TEST(MeshModule, ComputeGrowCapacity) {
    const mesh::MeshAllocatorSettings s;
    // 10 -> need 15 with 1.5x growth: 10*1.5 = 15.
    auto r = mesh::compute_grow_capacity(10, 15, s, 4);
    EXPECT_EQ(r.second, mesh::SlabGrowthResultKind::NeededGrowth);
    EXPECT_EQ(r.first, 15u);
    // Already large enough.
    r = mesh::compute_grow_capacity(10, 5, s, 4);
    EXPECT_EQ(r.second, mesh::SlabGrowthResultKind::NoGrowthNeeded);
    EXPECT_EQ(r.first, 10u);
    // Growth capped by max_slab_size: max_cap = 16 / 4 = 4 slots; can't grow past 4.
    mesh::MeshAllocatorSettings tiny = s;
    tiny.max_slab_size                = 16;
    r = mesh::compute_grow_capacity(4, 100, tiny, 4);
    EXPECT_EQ(r.second, mesh::SlabGrowthResultKind::CantGrow);
    EXPECT_EQ(r.first, 4u);
}

// compute_allocation mirrors Bevy MeshAllocator::allocate decision.
TEST(MeshModule, ComputeAllocationDecision) {
    const mesh::MeshAllocatorSettings s;
    // float3 vertex (size 12, 1 elem/slot), 24 bytes (2 vertices): 2 slots, general.
    const auto v12 = mesh::ElementLayout::make(mesh::ElementClass::Vertex, 12);
    auto r         = mesh::compute_allocation(24, v12, s);
    EXPECT_EQ(r.first, 2u);
    EXPECT_FALSE(r.second);
    // u16 index (size 2, 2 elem/slot), 6 bytes (3 u16): ceil(3/2)=2 slots, general.
    const auto i16 = mesh::ElementLayout::make(mesh::ElementClass::Index, 2);
    r              = mesh::compute_allocation(6, i16, s);
    EXPECT_EQ(r.first, 2u);
    EXPECT_FALSE(r.second);
    // Payload large enough to exceed the 256 MiB large threshold -> own slab.
    r = mesh::compute_allocation(300ull * 1024 * 1024, v12, s);
    EXPECT_TRUE(r.second);
}

// General-slab element range (Bevy mesh_slice_in_slab).
TEST(MeshModule, GeneralSlabElementRange) {
    const auto v12 = mesh::ElementLayout::make(mesh::ElementClass::Vertex, 12);
    // offset 0, 2 slots -> elements [0, 2).
    auto range = mesh::general_slab_element_range(0, 2, v12);
    EXPECT_EQ(range.first, 0u);
    EXPECT_EQ(range.second, 2u);
    // u16 index layout (2 elem/slot): offset 3, 2 slots -> elements [6, 10).
    const auto i16 = mesh::ElementLayout::make(mesh::ElementClass::Index, 2);
    range          = mesh::general_slab_element_range(3, 2, i16);
    EXPECT_EQ(range.first, 6u);
    EXPECT_EQ(range.second, 10u);
}

// SlabAllocation is a value type holding slot offset + count.
TEST(MeshModule, SlabAllocationValueType) {
    EXPECT_EQ((mesh::SlabAllocation{3, 2}), (mesh::SlabAllocation{3, 2}));
    EXPECT_NE((mesh::SlabAllocation{3, 2}), (mesh::SlabAllocation{4, 2}));
    EXPECT_EQ(mesh::SlabAllocation{}.offset, 0u);
    EXPECT_EQ(mesh::SlabAllocation{}.slot_count, 0u);
}

// GeneralSlab packs allocations sequentially and reports emptiness.
TEST(MeshModule, GeneralSlabPacksAndGrows) {
    const mesh::MeshAllocatorSettings settings;
    const auto v12 = mesh::ElementLayout::make(mesh::ElementClass::Vertex, 12);
    auto slab      = mesh::GeneralSlab::make(v12, settings);
    EXPECT_GT(slab.current_slot_capacity, 0u);
    EXPECT_TRUE(slab.is_empty());

    auto a = slab.allocate(2, settings);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->offset, 0u);
    EXPECT_EQ(a->slot_count, 2u);
    EXPECT_FALSE(slab.is_empty());

    auto b = slab.allocate(3, settings);
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->offset, 2u);  // sequential cursor
    EXPECT_EQ(b->slot_count, 3u);
    EXPECT_EQ(slab.occupied_slots, 5u);
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
