#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <epix/app.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>
#include <epix/render/mesh/offset_allocator.hpp>
#include <limits>
#include <memory>
#include <webgpu/webgpu.hpp>

namespace mesh = epix::mesh;
namespace mesh_allocator = epix::render::mesh;

namespace {
bool initialize_mesh_allocator_test_app(epix::app::App& app) {
    auto instance = wgpu::createInstance();
    if (!instance) return false;
    auto adapter = instance.requestAdapter(wgpu::RequestAdapterOptions().setBackendType(wgpu::BackendType::eVulkan));
    if (!adapter) return false;
    auto device = adapter.requestDevice(wgpu::DeviceDescriptor{});
    if (!device) return false;
    auto queue = device.getQueue();
    if (!queue) return false;

    auto& world = app.world_mut();
    world.insert_resource(instance.clone());
    world.insert_resource(adapter.clone());
    world.insert_resource(device.clone());
    world.insert_resource(queue.clone());
    world.init_resource<mesh_allocator::MeshAllocatorSettings>();
    world.init_resource<mesh_allocator::MeshAllocator>();
    world.init_resource<mesh::MeshVertexBufferLayouts>();
    world.init_resource<epix::render::ExtractedAssets<mesh::Mesh>>();
    app.add_systems(epix::app::Update, epix::ecs::into(mesh_allocator::allocate_and_free_meshes));
    return true;
}
}  // namespace

// Bevy MeshVertexBufferLayout::array_stride: sum of attribute sizes.
TEST(MeshModule, VertexArrayStride) {
    // Without a color only the position attribute exists (12).
    auto plain = mesh::make_box2d(20.0f, 10.0f);
    EXPECT_EQ(mesh_allocator::vertex_array_stride(plain), 12u);
    // With a color: position (12) + color (16) = 28.
    auto box = mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f));
    EXPECT_EQ(mesh_allocator::vertex_array_stride(box), 28u);
}

// packed_vertex_bytes interleaves per-vertex attributes in ID order (Bevy).
TEST(MeshModule, PackedVertexBytesInterleave) {
    const auto box    = mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f, 0.5f, 0.25f, 0.0f));
    const auto packed = mesh_allocator::packed_vertex_bytes(box);
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

TEST(MeshModule, PackedVertexBytesPreservesSemanticIntegerValues) {
    constexpr mesh::MeshVertexAttribute custom{"Vertex_Custom_Snorm16x2", mesh::MeshVertexAttributeId{8},
                                               wgpu::VertexFormat::eSnorm16x2};
    mesh::Mesh value(wgpu::PrimitiveTopology::eTriangleList, epix::assets::RenderAssetUsages::RENDER_WORLD);
    value.insert_attribute(custom, mesh::VertexAttributeValues::Snorm16x2{{{1, -2}, {3, -4}}});

    const auto packed = mesh_allocator::packed_vertex_bytes(value);
    ASSERT_EQ(packed.size(), 4u * sizeof(std::int16_t));
    std::array<std::int16_t, 4> components{};
    std::memcpy(components.data(), packed.data(), packed.size());
    EXPECT_EQ(components, (std::array<std::int16_t, 4>{1, -2, 3, -4}));
}

// RenderMeshBufferInfo mirrors Bevy's Indexed/NonIndexed discriminator.
TEST(MeshModule, RenderMeshBufferInfoIndexedVersusNonIndexed) {
    const auto indexed = mesh_allocator::RenderMeshBufferInfo::indexed(6u, wgpu::IndexFormat::eUint16);
    EXPECT_TRUE(indexed.is_indexed());
    ASSERT_TRUE(indexed.indexed_info());
    EXPECT_EQ(indexed.indexed_info()->count, 6u);
    EXPECT_EQ(indexed.indexed_info()->index_format, wgpu::IndexFormat::eUint16);

    const auto non_indexed = mesh_allocator::RenderMeshBufferInfo::non_indexed();
    EXPECT_FALSE(non_indexed.is_indexed());
    EXPECT_EQ(non_indexed.indexed_info(), nullptr);

    // RenderMesh carries Bevy's metadata: count, buffer info, interned layout,
    // and topology; GPU buffers come from the MeshAllocator.
    mesh::MeshVertexBufferLayouts store;
    auto box    = mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f));
    auto render = mesh_allocator::RenderMesh{
        .vertex_count = static_cast<std::uint32_t>(box.count_vertices()),
        .buffer_info = mesh_allocator::RenderMeshBufferInfo::non_indexed(),
        .key_bits = mesh::BaseMeshPipelineKey::from_primitive_topology(wgpu::PrimitiveTopology::eTriangleList),
        .layout = box.get_mesh_vertex_buffer_layout(store),
    };
    EXPECT_FALSE(render.indexed());
    EXPECT_EQ(render.vertex_count, box.count_vertices());
    EXPECT_EQ(render.primitive_topology(), wgpu::PrimitiveTopology::eTriangleList);
    EXPECT_EQ(render.layout.value->layout().array_stride, 28u);
}

// Bevy RenderAsset::byte_len for RenderMesh: sum of per-vertex attribute
// strides * vertex count + index bytes. Used by the render-asset byte limiter.
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
    // (12 float3 + 16 float4) * 2 vertices + (3 * 2-byte u16 indices) = 62.
    EXPECT_EQ(*len, 62u);
}

// Bevy 0.18 MeshAllocatorSettings defaults.
TEST(MeshModule, MeshAllocatorSettingsMatchBevyDefaults) {
    const mesh_allocator::MeshAllocatorSettings settings;
    EXPECT_EQ(settings.min_slab_size, 1024ull * 1024);
    EXPECT_EQ(settings.max_slab_size, 1024ull * 1024 * 512);
    EXPECT_EQ(settings.large_threshold, 1024ull * 1024 * 256);
    EXPECT_EQ(settings.growth_factor, 1.5);
}

// Bevy installs MeshAllocatorSettings as an independently configurable
// render-world resource; MeshAllocator does not own a private settings copy.
TEST(MeshModule, MeshAllocatorPluginPreservesSettingsResource) {
    epix::app::App app = epix::app::App::create();
    auto render_app    = std::make_unique<epix::app::App>(epix::app::App::create());
    mesh_allocator::MeshAllocatorSettings custom;
    custom.min_slab_size = 4096;
    render_app->world_mut().insert_resource(custom);
    app.insert_sub_app(epix::render::Render, std::move(render_app));

    mesh_allocator::MeshAllocatorPlugin{}.attach(app);

    auto render = app.get_sub_app_mut(epix::render::Render);
    ASSERT_TRUE(render.has_value());
    const auto settings = render->get().world().get_resource<mesh_allocator::MeshAllocatorSettings>();
    ASSERT_TRUE(settings.has_value());
    EXPECT_EQ(settings->get().min_slab_size, 4096u);
    EXPECT_FALSE(render->get().world().get_resource<mesh_allocator::MeshAllocator>().has_value());

    // Bevy creates the allocator during Plugin::finish, after the adapter is
    // available. Epix's corresponding lifecycle phase is Plugin::ready.
    mesh_allocator::MeshAllocatorPlugin{}.ready(app);
    EXPECT_TRUE(render->get().world().get_resource<mesh_allocator::MeshAllocator>().has_value());
}

// Bevy SlabId / MeshBufferSlice are value types; verify identity + element range.
TEST(MeshModule, SlabIdAndMeshBufferSliceTypes) {
    const mesh_allocator::SlabId zero;
    const mesh_allocator::SlabId id{42};
    EXPECT_EQ(zero, mesh_allocator::SlabId{});
    EXPECT_EQ(id, mesh_allocator::SlabId{42});
    EXPECT_NE(id, mesh_allocator::SlabId{43});
    EXPECT_EQ(id.get(), 42u);
    EXPECT_THROW((void)mesh_allocator::SlabId{std::numeric_limits<std::uint32_t>::max()}, std::invalid_argument);
    const mesh_allocator::MeshBufferSlice slice{nullptr, {3u, 9u}};
    EXPECT_EQ(slice.buffer, nullptr);
    EXPECT_EQ(slice.range.first, 3u);
    EXPECT_EQ(slice.range.second, 9u);
}

// Empty MeshAllocator exposes Bevy's query surface with no allocations.
TEST(MeshModule, MeshAllocatorEmptyQuerySurface) {
    static_assert(std::movable<mesh_allocator::MeshAllocator>);
    static_assert(!std::copy_constructible<mesh_allocator::MeshAllocator>);
    epix::ecs::World world(1);
    world.init_resource<mesh_allocator::MeshAllocator>();
    const auto& allocator = world.resource<mesh_allocator::MeshAllocator>();
    EXPECT_EQ(allocator.slab_count(), 0u);
    EXPECT_EQ(allocator.slabs_size(), 0u);
    EXPECT_EQ(allocator.allocations(), 0u);
}

// Device-backed public-system coverage for Bevy's allocate-all → grow-once →
// upload-all path, public slices, extra usages, and removal handling.
TEST(MeshModule, AllocateAndFreeMeshesMatchesBevyFrameFlow) {
    epix::app::App app = epix::app::App::create();
    if (!initialize_mesh_allocator_test_app(app)) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping device test";
        return;
    }

    auto& world                                                   = app.world_mut();
    auto& settings = world.resource_mut<mesh_allocator::MeshAllocatorSettings>();
    settings.min_slab_size                                        = 1;
    settings.max_slab_size                                        = 1024 * 1024;
    settings.large_threshold                                      = 1024 * 1024;
    world.resource_mut<mesh_allocator::MeshAllocator>().extra_buffer_usages = wgpu::BufferUsage::eStorage;

    epix::assets::Assets<mesh::Mesh> store;
    const auto id1 = store.add(mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f))).id();
    const auto id2 = store.add(mesh::make_box2d(30.0f, 15.0f, glm::vec4(0.5f))).id();
    auto first     = store.remove_untracked(id1);
    auto second    = store.remove_untracked(id2);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    auto& extracted = world.resource_mut<epix::render::ExtractedAssets<mesh::Mesh>>();
    extracted.extracted.emplace_back(id1, std::move(*first));
    extracted.extracted.emplace_back(id2, std::move(*second));
    extracted.added.insert(id1);
    extracted.added.insert(id2);

    ASSERT_TRUE(app.run_schedule(epix::app::Update));

    const auto& allocator = world.resource<mesh_allocator::MeshAllocator>();
    const auto vertex1    = allocator.mesh_vertex_slice(id1);
    const auto vertex2    = allocator.mesh_vertex_slice(id2);
    ASSERT_TRUE(vertex1.has_value());
    ASSERT_TRUE(vertex2.has_value());
    EXPECT_EQ(vertex1->buffer, vertex2->buffer);  // same layout packs together
    EXPECT_EQ(vertex1->range.second - vertex1->range.first, 4u);
    EXPECT_EQ(vertex2->range.second - vertex2->range.first, 4u);
    EXPECT_GT(allocator.slabs_size(), 0u);
    const auto usage = static_cast<std::uint64_t>(vertex1->buffer->getUsage());
    EXPECT_NE(usage & static_cast<std::uint64_t>(wgpu::BufferUsage::eStorage), 0u);

    epix::render::ExtractedAssets<mesh::Mesh> removals;
    removals.removed.insert(id1);
    removals.removed.insert(id2);
    world.insert_resource(std::move(removals));
    ASSERT_TRUE(app.run_schedule(epix::app::Update));
    EXPECT_FALSE(world.resource<mesh_allocator::MeshAllocator>().mesh_vertex_slice(id1).has_value());
    EXPECT_FALSE(world.resource<mesh_allocator::MeshAllocator>().mesh_vertex_slice(id2).has_value());
    EXPECT_EQ(world.resource<mesh_allocator::MeshAllocator>().slab_count(), 0u);
}

// Payloads above the threshold receive dedicated buffers; the public API
// exposes the complete range and carries extra usages to those buffers too.
TEST(MeshModule, AllocateAndFreeMeshesUsesLargeObjectSlabs) {
    epix::app::App app = epix::app::App::create();
    if (!initialize_mesh_allocator_test_app(app)) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping device test";
        return;
    }
    auto& world                                                       = app.world_mut();
    world.resource_mut<mesh_allocator::MeshAllocatorSettings>().large_threshold = 32;
    world.resource_mut<mesh_allocator::MeshAllocator>().extra_buffer_usages     = wgpu::BufferUsage::eStorage;

    epix::assets::Assets<mesh::Mesh> store;
    auto source                      = mesh::make_box2d(20.0f, 10.0f, glm::vec4(1.0f));
    const auto expected_vertex_count = source.count_vertices();
    const auto id                    = store.add(std::move(source)).id();
    auto extracted_mesh              = store.remove_untracked(id);
    ASSERT_TRUE(extracted_mesh.has_value());
    auto& extracted = world.resource_mut<epix::render::ExtractedAssets<mesh::Mesh>>();
    extracted.extracted.emplace_back(id, std::move(*extracted_mesh));
    extracted.added.insert(id);

    ASSERT_TRUE(app.run_schedule(epix::app::Update));
    const auto& allocator = world.resource<mesh_allocator::MeshAllocator>();
    const auto vertex     = allocator.mesh_vertex_slice(id);
    ASSERT_TRUE(vertex.has_value());
    EXPECT_EQ(vertex->range.first, 0u);
    EXPECT_EQ(vertex->range.second, expected_vertex_count);
    const auto usage = static_cast<std::uint64_t>(vertex->buffer->getUsage());
    EXPECT_NE(usage & static_cast<std::uint64_t>(wgpu::BufferUsage::eStorage), 0u);
}

// ElementLayout slot math (Bevy: slot size is a multiple of both the element
// size and the 4-byte copy-buffer alignment).
TEST(MeshModule, ElementLayoutSlotMath) {
    // stride 12 (float3 vertex) -> 1 element/slot, 12-byte slot.
    const auto v = mesh_allocator::detail::ElementLayout::make(mesh_allocator::detail::ElementClass::Vertex, 12);
    EXPECT_EQ(v.elements_per_slot, 1u);
    EXPECT_EQ(v.slot_size(), 12u);
    // stride 2 (u16 index) -> 2 elements/slot (slot 4, divisible by 4).
    const auto i16 = mesh_allocator::detail::ElementLayout::make(mesh_allocator::detail::ElementClass::Index, 2);
    EXPECT_EQ(i16.elements_per_slot, 2u);
    EXPECT_EQ(i16.slot_size(), 4u);
    // stride 4 -> 1 element/slot, 4-byte slot.
    EXPECT_EQ(mesh_allocator::detail::ElementLayout::make(mesh_allocator::detail::ElementClass::Index, 4).slot_size(),
              4u);
    // stride 6 -> 2 elements/slot (slot 12, divisible by 4).
    const auto v6 = mesh_allocator::detail::ElementLayout::make(mesh_allocator::detail::ElementClass::Vertex, 6);
    EXPECT_EQ(v6.elements_per_slot, 2u);
    EXPECT_EQ(v6.slot_size(), 12u);
    // stride 1 (u8) -> 4 elements/slot (slot 4).
    EXPECT_EQ(mesh_allocator::detail::ElementLayout::make(mesh_allocator::detail::ElementClass::Vertex, 1).slot_size(),
              4u);
}

// compute_grow_capacity mirrors Bevy GeneralSlab::grow_if_necessary.
TEST(MeshModule, ComputeGrowCapacity) {
    const mesh_allocator::MeshAllocatorSettings s;
    // 10 -> need 15 with 1.5x growth: 10*1.5 = 15.
    auto r = mesh_allocator::detail::compute_grow_capacity(10, 15, s, 4);
    ASSERT_TRUE(std::holds_alternative<mesh_allocator::detail::SlabGrowthNeeded>(r.second));
    EXPECT_EQ(std::get<mesh_allocator::detail::SlabGrowthNeeded>(r.second).slab_to_reallocate.old_slot_capacity,
              10u);
    EXPECT_EQ(r.first, 15u);
    // Already large enough.
    r = mesh_allocator::detail::compute_grow_capacity(10, 5, s, 4);
    EXPECT_TRUE(std::holds_alternative<mesh_allocator::detail::SlabGrowthNoGrowthNeeded>(r.second));
    EXPECT_EQ(r.first, 10u);
    // Growth capped by max_slab_size: max_cap = 16 / 4 = 4 slots; can't grow past 4.
    mesh_allocator::MeshAllocatorSettings tiny = s;
    tiny.max_slab_size               = 16;
    r = mesh_allocator::detail::compute_grow_capacity(4, 100, tiny, 4);
    EXPECT_TRUE(std::holds_alternative<mesh_allocator::detail::SlabGrowthCantGrow>(r.second));
    EXPECT_EQ(r.first, 4u);
}

// compute_allocation mirrors Bevy MeshAllocator::allocate decision.
TEST(MeshModule, ComputeAllocationDecision) {
    const mesh_allocator::MeshAllocatorSettings s;
    // float3 vertex (size 12, 1 elem/slot), 24 bytes (2 vertices): 2 slots, general.
    const auto v12 =
        mesh_allocator::detail::ElementLayout::make(mesh_allocator::detail::ElementClass::Vertex, 12);
    auto r = mesh_allocator::detail::compute_allocation(24, v12, s);
    EXPECT_EQ(r.first, 2u);
    EXPECT_FALSE(r.second);
    // u16 index (size 2, 2 elem/slot), 6 bytes (3 u16): ceil(3/2)=2 slots, general.
    const auto i16 = mesh_allocator::detail::ElementLayout::make(mesh_allocator::detail::ElementClass::Index, 2);
    r              = mesh_allocator::detail::compute_allocation(6, i16, s);
    EXPECT_EQ(r.first, 2u);
    EXPECT_FALSE(r.second);
    // Payload large enough to exceed the 256 MiB large threshold -> own slab.
    r = mesh_allocator::detail::compute_allocation(300ull * 1024 * 1024, v12, s);
    EXPECT_TRUE(r.second);
}

// General-slab element range (Bevy mesh_slice_in_slab).
TEST(MeshModule, GeneralSlabElementRange) {
    const auto v12 =
        mesh_allocator::detail::ElementLayout::make(mesh_allocator::detail::ElementClass::Vertex, 12);
    // offset 0, 2 slots -> elements [0, 2).
    auto range = mesh_allocator::detail::general_slab_element_range(0, 2, v12);
    EXPECT_EQ(range.first, 0u);
    EXPECT_EQ(range.second, 2u);
    // u16 index layout (2 elem/slot): offset 3, 2 slots -> elements [6, 10).
    const auto i16 = mesh_allocator::detail::ElementLayout::make(mesh_allocator::detail::ElementClass::Index, 2);
    range = mesh_allocator::detail::general_slab_element_range(3, 2, i16);
    EXPECT_EQ(range.first, 6u);
    EXPECT_EQ(range.second, 10u);
}

// SlabAllocation is a value type holding the offset-allocator handle + count.
TEST(MeshModule, SlabAllocationValueType) {
    const mesh_allocator::offset_allocator::Allocation a0{0u, 1u};
    const mesh_allocator::offset_allocator::Allocation a1{2u, 1u};
    EXPECT_EQ((mesh_allocator::detail::SlabAllocation{a0, 2}),
              (mesh_allocator::detail::SlabAllocation{0u, 1u, 2}));
    EXPECT_NE((mesh_allocator::detail::SlabAllocation{a0, 2}), (mesh_allocator::detail::SlabAllocation{a1, 2}));
    EXPECT_EQ(mesh_allocator::detail::SlabAllocation{}.offset(), 0u);
    EXPECT_EQ(mesh_allocator::detail::SlabAllocation{}.slot_count, 0u);
}

// offset_allocator (Bevy 0.18 mesh slab allocator) freed gaps are reused.
TEST(MeshModule, OffsetAllocatorAllocatesContiguouslyAndFrees) {
    mesh_allocator::offset_allocator::Allocator allocator(64);
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
    mesh_allocator::offset_allocator::Allocator allocator(64);
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
    mesh_allocator::offset_allocator::Allocator allocator(64);
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
    EXPECT_EQ(mesh_allocator::offset_allocator::min_allocator_size(1), 1u);
    EXPECT_EQ(mesh_allocator::offset_allocator::min_allocator_size(8), 8u);
    // Required to hold N slots: returns the bin's capacity.
    const auto min = mesh_allocator::offset_allocator::min_allocator_size(7);
    EXPECT_GE(min, 7u);
}

// Bevy retains the slab capacity from the beginning of the frame when one
// slab grows repeatedly, so the eventual GPU copy is scheduled only once and
// copies only the old resident range.
TEST(MeshModule, SlabsToReallocatePreservesInitialCapacity) {
    mesh_allocator::MeshAllocatorSettings settings;
    settings.min_slab_size = 40;
    settings.max_slab_size = 4096;
    const auto layout =
        mesh_allocator::detail::ElementLayout::make(mesh_allocator::detail::ElementClass::Vertex, 4);
    auto slab = mesh_allocator::detail::GeneralSlab::make(layout, 1, settings);
    ASSERT_EQ(slab.current_slot_capacity, 10u);

    mesh_allocator::detail::SlabsToReallocate pending;
    const auto first = slab.grow_if_necessary(15, settings);
    ASSERT_TRUE(std::holds_alternative<mesh_allocator::detail::SlabGrowthNeeded>(first));
    pending.slabs.try_emplace(
        mesh_allocator::SlabId{7},
        std::get<mesh_allocator::detail::SlabGrowthNeeded>(first).slab_to_reallocate);
    const auto second = slab.grow_if_necessary(22, settings);
    ASSERT_TRUE(std::holds_alternative<mesh_allocator::detail::SlabGrowthNeeded>(second));
    pending.slabs.try_emplace(
        mesh_allocator::SlabId{7},
        std::get<mesh_allocator::detail::SlabGrowthNeeded>(second).slab_to_reallocate);

    ASSERT_EQ(pending.slabs.size(), 1u);
    EXPECT_EQ(pending.slabs.at(mesh_allocator::SlabId{7}).old_slot_capacity, 10u);
    EXPECT_GE(slab.current_slot_capacity, 22u);
}

// Bevy rounds byte limits up to slots and guarantees that the backing offset
// allocator can hold the first payload even when its size-class capacity is
// larger than the configured maximum.
TEST(MeshModule, GeneralSlabCapacityMatchesBevyRounding) {
    mesh_allocator::MeshAllocatorSettings settings;
    settings.min_slab_size = 5;
    settings.max_slab_size = 5;
    const auto layout =
        mesh_allocator::detail::ElementLayout::make(mesh_allocator::detail::ElementClass::Vertex, 4);
    auto slab = mesh_allocator::detail::GeneralSlab::make(layout, 7, settings);
    EXPECT_EQ(slab.current_slot_capacity, mesh_allocator::offset_allocator::min_allocator_size(7));
    const auto allocation = slab.allocator.allocate(7);
    ASSERT_TRUE(allocation.has_value());
    EXPECT_EQ(allocation->offset, 0u);
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
    const auto source_attributes = source.try_attributes();
    ASSERT_FALSE(source_attributes.has_value());
    EXPECT_EQ(source_attributes.error(), mesh::MeshAccessError::ExtractedToRenderWorld);
    EXPECT_THROW(source.count_vertices(), std::logic_error);

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

    auto source_mesh        = mesh::make_box2d(24.0f, 12.0f);
    source_mesh.asset_usage = epix::assets::RenderAssetUsages::RENDER_WORLD;
    auto handle = main_world.resource_mut<epix::assets::Assets<mesh::Mesh>>().emplace(std::move(source_mesh));
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
    const auto source_attributes = source->get().try_attributes();
    ASSERT_FALSE(source_attributes.has_value());
    EXPECT_EQ(source_attributes.error(), mesh::MeshAccessError::ExtractedToRenderWorld);
}

TEST(MeshModule, ExtractSystemClonesDualWorldMesh) {
    struct ResourceIdShiftA {};
    struct ResourceIdShiftB {};
    struct ResourceIdShiftC {};

    epix::ecs::World main_world(7);
    epix::ecs::World render_world(7);
    render_world.insert_resource(epix::app::ExtractedWorld{main_world});
    render_world.insert_resource(epix::render::ExtractedAssets<mesh::Mesh>{});
    render_world.insert_resource(epix::render::RenderAssets<mesh::Mesh>{});
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
    EXPECT_GT(extracted.extracted.front().second.count_vertices(), 0u);
    const auto source = main_world.resource<epix::assets::Assets<mesh::Mesh>>().get(handle.id());
    ASSERT_TRUE(source.has_value());
    EXPECT_GT(source->get().count_vertices(), 0u);
}
