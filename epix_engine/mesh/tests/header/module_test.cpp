#include <gtest/gtest.h>

#include <epix/app.hpp>
#include <epix/mesh.hpp>
#include <epix/mesh/offset_allocator.hpp>
#include <epix/render.hpp>
#include <webgpu/webgpu.hpp>

namespace mesh = epix::mesh;

// Bevy MeshVertexBufferLayout::array_stride: sum of attribute sizes.
TEST(MeshModule, VertexArrayStride) {
    // Without a color only the position attribute exists (12).
    auto plain = mesh::make_box2d(20.0f, 10.0f);
    EXPECT_EQ(mesh::vertex_array_stride(plain), 12u);
    // With a color: position (12) + color (16) = 28.
    auto box = mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f));
    EXPECT_EQ(mesh::vertex_array_stride(box), 28u);
}

// packed_vertex_bytes interleaves per-vertex attributes in slot order (Bevy).
TEST(MeshModule, PackedVertexBytesInterleave) {
    const auto box = mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f, 0.5f, 0.25f, 0.0f));
    const auto packed = mesh::packed_vertex_bytes(box);
    // 4 verts * (12 pos + 16 color) = 112 bytes.
    EXPECT_EQ(packed.size(), 112u);
    // Vertex 0's color begins at byte 12 (position is 12 bytes).
    float red = 0.0f;
    std::memcpy(&red, packed.data() + 12, sizeof(float));
    EXPECT_FLOAT_EQ(red, 1.0f);
    float green = 0.0f;
    std::memcpy(&green, packed.data() + 16, sizeof(float));
    EXPECT_FLOAT_EQ(green, 0.5f);
}

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
    epix::assets::Assets<mesh::Mesh> store;
    const auto id_a = store.add(mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f))).id();
    const auto id_b = store.add(mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f))).id();
    mesh::MeshAllocator allocator;
    auto a = allocator.allocate(id_a, 24, v12);  // 2 slots
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->first.value, 0u);
    EXPECT_EQ(a->second.offset(), 0u);
    EXPECT_EQ(allocator.slab_count(), 1u);
    EXPECT_GT(allocator.slabs_size(), 0u);
    // Same layout packs into the existing slab at the next slot.
    auto b = allocator.allocate(id_b, 36, v12);  // 3 slots
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->first, a->first);
    EXPECT_EQ(b->second.offset(), 2u);
    EXPECT_EQ(allocator.slab_count(), 1u);
    // Different layout -> a new slab.
    const auto i16 = mesh::ElementLayout::make(mesh::ElementClass::Index, 2);
    const auto id_c = store.add(mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f))).id();
    auto c         = allocator.allocate(id_c, 2, i16);
    ASSERT_TRUE(c.has_value());
    EXPECT_NE(c->first, a->first);
    EXPECT_EQ(allocator.slab_count(), 2u);
}

// Device-backed: create a real wgpu device and verify the allocator creates a
// slab GPU buffer and exposes a slice with the correct element range.
TEST(MeshModule, MeshAllocatorDeviceBufferAndSlice) {
    epix::app::App app = epix::app::App::create();
    app.add_events<epix::window::WindowClosed>();
    try {
        epix::render::RenderPlugin{}.attach(app);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping device test: " << e.what();
        return;
    }
    auto render_sub = app.take_sub_app(epix::render::Render);
    ASSERT_TRUE(render_sub);
    const auto& device = render_sub->world().resource<wgpu::Device>();
    const auto& queue  = render_sub->world().resource<wgpu::Queue>();

    mesh::MeshAllocator allocator;
    allocator.settings.min_slab_size = 1;  // force growth on small payloads
    const auto v12 = mesh::ElementLayout::make(mesh::ElementClass::Vertex, 12);
    epix::assets::Assets<mesh::Mesh> store;
    const auto id1 = store.add(mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f))).id();
    auto a         = allocator.allocate(id1, 24, v12);  // 2 slots
    ASSERT_TRUE(a.has_value());
    const auto buffer =
        allocator.ensure_slab_buffer(device, queue, a->first, wgpu::BufferUsage::eVertex);
    EXPECT_TRUE(buffer);
    EXPECT_EQ(buffer.getSize(), 24u);  // initial capacity == 2 slots * 12

    const std::uint8_t data[24] = {0};
    allocator.upload_to_slab(device, queue, a->first, a->second, id1, data, sizeof(data), wgpu::BufferUsage::eVertex);
    EXPECT_EQ(allocator.mesh_vertex_slice(id1)->begin, 0u);
    EXPECT_EQ(allocator.mesh_vertex_slice(id1)->end, 2u);

    // A second mesh that grows the slab triggers the Bevy reallocate path: a
    // new, larger buffer is created and the old contents copied across.
    const auto id2 = store.add(mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f))).id();
    auto b         = allocator.allocate(id2, 36, v12);  // +3 slots (5 total)
    ASSERT_TRUE(b.has_value());
    const auto grown = allocator.ensure_slab_buffer(device, queue, b->first, wgpu::BufferUsage::eVertex);
    EXPECT_EQ(grown.getSize(), 60u);  // 5 slots * 12
    allocator.upload_to_slab(device, queue, b->first, b->second, id2, data, sizeof(data),
                             wgpu::BufferUsage::eVertex);
    const auto slice = allocator.mesh_vertex_slice(id1);
    ASSERT_TRUE(slice.has_value());
    EXPECT_EQ(slice->begin, 0u);
    EXPECT_EQ(slice->end, 2u);

    // Compute the expected element range directly; verify the range helper
    // matches the allocation.
    const auto range = mesh::general_slab_element_range(a->second.offset(), a->second.slot_count, v12);
    EXPECT_EQ(range.first, 0u);
    EXPECT_EQ(range.second, 2u);
    app.insert_sub_app(epix::render::Render, std::move(render_sub));
}

// Device-backed: payloads above the large threshold get their own dedicated
// slab, created and filled via the mapped-at-creation path (Bevy
// copy_element_data for Slab::LargeObject).
TEST(MeshModule, MeshAllocatorLargeObjectSlab) {
    epix::app::App app = epix::app::App::create();
    app.add_events<epix::window::WindowClosed>();
    try {
        epix::render::RenderPlugin{}.attach(app);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping device test: " << e.what();
        return;
    }
    auto render_sub = app.take_sub_app(epix::render::Render);
    ASSERT_TRUE(render_sub);
    const auto& device = render_sub->world().resource<wgpu::Device>();
    const auto& queue  = render_sub->world().resource<wgpu::Queue>();

    mesh::MeshAllocator allocator;
    allocator.settings.large_threshold = 32;  // tiny threshold -> large-object path
    const auto v12 = mesh::ElementLayout::make(mesh::ElementClass::Vertex, 12);
    epix::assets::Assets<mesh::Mesh> store;
    const auto id    = store.add(mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f))).id();
    const auto alloc = allocator.allocate(id, 48, v12);
    ASSERT_TRUE(alloc.has_value());
    const auto* slab = allocator.slabs.at(alloc->first).large();
    ASSERT_TRUE(slab);  // dedicated large-object slab, not general
    const std::uint8_t data[48] = {0};
    allocator.upload_to_slab(device, queue, alloc->first, alloc->second, id, data, sizeof(data),
                             wgpu::BufferUsage::eVertex);
    const auto slice = allocator.mesh_vertex_slice(id);
    ASSERT_TRUE(slice.has_value());
    EXPECT_EQ(slice->begin, 0u);
    EXPECT_EQ(slice->end, 4u);  // 48 bytes / 12-byte elements
    EXPECT_EQ(slice->buffer, std::addressof(allocator.slabs.at(alloc->first).large()->buffer));
    app.insert_sub_app(epix::render::Render, std::move(render_sub));
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

// SlabAllocation is a value type holding the offset-allocator handle + count.
TEST(MeshModule, SlabAllocationValueType) {
    const mesh::offset_allocator::Allocation a0{0u, 1u};
    const mesh::offset_allocator::Allocation a1{2u, 1u};
    EXPECT_EQ((mesh::SlabAllocation{a0, 2}), (mesh::SlabAllocation{0u, 1u, 2}));
    EXPECT_NE((mesh::SlabAllocation{a0, 2}), (mesh::SlabAllocation{a1, 2}));
    EXPECT_EQ(mesh::SlabAllocation{}.offset(), 0u);
    EXPECT_EQ(mesh::SlabAllocation{}.slot_count, 0u);
}

// offset_allocator (Bevy 0.18 mesh slab allocator) freed gaps are reused.
TEST(MeshModule, OffsetAllocatorAllocatesContiguouslyAndFrees) {
    mesh::offset_allocator::Allocator allocator(64);
    auto a = allocator.allocate(4);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->offset, 0u);
    auto b = allocator.allocate(4);
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->offset, 4u);
    auto report = allocator.storage_report();
    EXPECT_EQ(report.total_free_space, 56u);

    allocator.free(*a);
    allocator.free(*b);
    report = allocator.storage_report();
    EXPECT_EQ(report.total_free_space, 64u);
    EXPECT_EQ(report.largest_free_region, 64u);
    EXPECT_TRUE(allocator.is_empty());
}

// The key Bevy behavior absent from a sequential cursor: a freed gap inside a
// slab is reused by the next allocation.
TEST(MeshModule, OffsetAllocatorReusesFreedGaps) {
    mesh::offset_allocator::Allocator allocator(64);
    auto a = allocator.allocate(4);  // offset 0
    auto b = allocator.allocate(8);  // offset 4
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->offset, 4u);

    allocator.free(*a);
    auto c = allocator.allocate(4);
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->offset, 0u);  // reuses the freed gap, not the tail
    allocator.free(*b);
    allocator.free(*c);
    EXPECT_TRUE(allocator.is_empty());
}

// Adjacent freed allocations merge back into one free region, which is then
// reused before the untouched (larger) tail.
TEST(MeshModule, OffsetAllocatorMergesNeighbors) {
    mesh::offset_allocator::Allocator allocator(64);
    auto a = allocator.allocate(4);  // offset 0
    auto b = allocator.allocate(8);  // offset 4
    auto c = allocator.allocate(4);  // offset 12 (kept alive)
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->offset, 12u);

    allocator.free(*a);
    allocator.free(*b);
    // a+b merged into one 12-unit region at offset 0; the 48-unit tail is
    // untouched but sits in a higher size class, so the next fit reuses the
    // merged gap at offset 0.
    auto d = allocator.allocate(8);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->offset, 0u);
    allocator.free(*c);
    allocator.free(*d);
    EXPECT_TRUE(allocator.is_empty());
}

// min_allocator_size mirrors offset-allocator ext::min_allocator_size.
TEST(MeshModule, OffsetAllocatorMinAllocatorSize) {
    EXPECT_EQ(mesh::offset_allocator::min_allocator_size(1), 1u);
    EXPECT_EQ(mesh::offset_allocator::min_allocator_size(8), 8u);
    // Required to hold N slots: returns the bin's capacity.
    const auto min = mesh::offset_allocator::min_allocator_size(7);
    EXPECT_GE(min, 7u);
}

// GeneralSlab packs allocations and is non-empty while payloads are pending.
TEST(MeshModule, GeneralSlabPacksAndGrows) {
    const mesh::MeshAllocatorSettings settings;
    const auto v12 = mesh::ElementLayout::make(mesh::ElementClass::Vertex, 12);
    epix::assets::Assets<mesh::Mesh> store;
    const auto id1   = store.add(mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f))).id();
    const auto id2   = store.add(mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f))).id();
    mesh::MeshAllocator allocator;
    auto a = allocator.allocate(id1, 24, v12);  // 2 slots
    ASSERT_TRUE(a.has_value());
    const auto* slab = allocator.slabs.at(a->first).general();
    ASSERT_TRUE(slab);
    EXPECT_GT(slab->current_slot_capacity, 0u);
    EXPECT_FALSE(slab->is_empty());  // pending allocation recorded

    auto b = allocator.allocate(id2, 36, v12);  // 3 slots
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->first, a->first);
    EXPECT_EQ(b->second.offset(), 2u);
    EXPECT_EQ(allocator.slab_count(), 1u);
}

// Bevy's offset-allocator semantics at the MeshAllocator level: freed slots
// inside a still-live slab are reused, and a fully-empty slab is removed.
TEST(MeshModule, MeshAllocatorFreeReusesFreedSlots) {
    const mesh::MeshAllocatorSettings settings;
    const auto v12 = mesh::ElementLayout::make(mesh::ElementClass::Vertex, 12);
    epix::assets::Assets<mesh::Mesh> store;
    const auto id1 = store.add(mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f))).id();
    const auto id2 = store.add(mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f))).id();

    mesh::MeshAllocator allocator;
    auto a1 = allocator.allocate(id1, 24, v12);  // 2 slots at offset 0
    ASSERT_TRUE(a1.has_value());
    auto a2 = allocator.allocate(id2, 24, v12);  // 2 slots right after
    ASSERT_TRUE(a2.has_value());
    EXPECT_EQ(a1->second.offset(), 0u);
    EXPECT_EQ(a2->second.offset(), 2u);
    EXPECT_EQ(allocator.slab_count(), 1u);
    EXPECT_EQ(allocator.allocations(), 0u);  // Bevy counts index allocations only

    // Freeing one mesh keeps the slab live with id2 still resident.
    allocator.free_all(id1);
    EXPECT_EQ(allocator.slab_count(), 1u);

    // A new mesh reuses the freed gap (offset-allocator), no new slab.
    const auto id3 = store.add(mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f))).id();
    auto a3        = allocator.allocate(id3, 24, v12);
    ASSERT_TRUE(a3.has_value());
    EXPECT_EQ(a3->first, a1->first);
    EXPECT_EQ(a3->second.offset(), 0u);
    EXPECT_EQ(allocator.slab_count(), 1u);

    // Freeing everything removes the empty slab entirely (Bevy keeps no pool).
    allocator.free_all(id2);
    allocator.free_all(id3);
    EXPECT_EQ(allocator.slab_count(), 0u);
    EXPECT_TRUE(allocator.slab_layouts.empty());
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
