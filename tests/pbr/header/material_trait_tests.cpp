#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/core_graph.hpp>
#include <epix/image.hpp>
#include <epix/pbr/material.hpp>
#include <epix/pbr/material_bind_groups.hpp>
#include <epix/pbr/material_plugin.hpp>
#include <epix/render.hpp>
#include <epix/render/alpha.hpp>
#include <epix/shader.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/window.hpp>
#include <gtest/gtest.h>
#include <memory>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

using namespace epix::app;
using namespace epix::assets;

namespace pbr    = epix::pbr;
namespace render = epix::render;
namespace ecs    = epix::ecs;

namespace {
/** @brief A material that overrides nothing: every member must then answer with
 *  Bevy's default. */
struct PlainMaterial : pbr::Material<PlainMaterial> {};

/** @brief A material that overrides everything a material can. */
struct CustomMaterial : pbr::Material<CustomMaterial> {
    using Data = int;

    static std::optional<std::string_view> vertex_shader() { return "custom/vertex.slang"; }
    static std::optional<std::string_view> fragment_shader() { return "custom/fragment.slang"; }
    render::AlphaMode alpha_mode() const { return render::AlphaMode::mask(0.25f); }
    pbr::OpaqueRendererMethod opaque_render_method() const { return pbr::OpaqueRendererMethod::Deferred; }
    float depth_bias() const { return 1.5f; }
    bool reads_view_transmission_texture() const { return true; }
    static bool enable_prepass() { return false; }
    static bool enable_shadows() { return false; }
};
}  // namespace

/** @brief The 3D material trait answers with Bevy's defaults (Bevy `Material`,
 *  `bevy_pbr/src/material.rs:138-273`).
 *
 *  A default in a trait is invisible at the call site, so a port can get one wrong
 *  without any test noticing: `enable_prepass` defaults to **true**, for instance,
 *  which is the opposite of what "prepasses are optional" suggests. This pins the
 *  defaults directly, and needs no device - the trait is entirely static data and
 *  dispatch. */
TEST(MaterialTrait, DefaultsMatchBevyAndOverridesWin) {
    const PlainMaterial plain;
    EXPECT_EQ(plain.alpha_mode(), render::AlphaMode::opaque());
    EXPECT_EQ(plain.opaque_render_method(), pbr::OpaqueRendererMethod::Forward);
    EXPECT_FLOAT_EQ(plain.depth_bias(), 0.0f);
    EXPECT_FALSE(plain.reads_view_transmission_texture());

    EXPECT_EQ(PlainMaterial::vertex_shader(), std::nullopt) << "Bevy's ShaderRef::Default is `nullopt` here";
    EXPECT_EQ(PlainMaterial::fragment_shader(), std::nullopt);
    EXPECT_TRUE(PlainMaterial::enable_prepass()) << "Bevy's default is true, which is easy to invert";
    EXPECT_TRUE(PlainMaterial::enable_shadows());
    EXPECT_EQ(PlainMaterial::prepass_vertex_shader(), std::nullopt);
    EXPECT_EQ(PlainMaterial::deferred_fragment_shader(), std::nullopt);

    // The overrides answer on the material itself. This port's CRTP is static
    // dispatch, so a call through `Material<M>&` reaches the *base* defaults - which
    // is why the pipeline holds `M` and not the base (the 2D counterpart works the
    // same way). Asserting it here keeps the design honest rather than discovering
    // it in a pipeline.
    const CustomMaterial custom;
    EXPECT_EQ(custom.alpha_mode(), render::AlphaMode::mask(0.25f));
    EXPECT_EQ(custom.opaque_render_method(), pbr::OpaqueRendererMethod::Deferred);
    EXPECT_FLOAT_EQ(custom.depth_bias(), 1.5f);
    EXPECT_TRUE(custom.reads_view_transmission_texture());
    const pbr::Material<CustomMaterial>& as_base = custom;
    EXPECT_EQ(as_base.alpha_mode(), render::AlphaMode::opaque())
        << "static dispatch: the base answers with its own default";
    EXPECT_FALSE(CustomMaterial::enable_prepass());
    EXPECT_FALSE(CustomMaterial::enable_shadows());
    EXPECT_EQ(CustomMaterial::vertex_shader(), std::optional<std::string_view>{"custom/vertex.slang"});

    // The associated data type is Bevy's `M::Data`, and absent means monostate.
    static_assert(std::is_same_v<pbr::MaterialData<CustomMaterial>::type, int>);
    static_assert(std::is_same_v<pbr::MaterialData<PlainMaterial>::type, std::monostate>);

    // The key carries the mesh key and that data, and compares by them.
    const pbr::MaterialPipelineKey<CustomMaterial> key{.mesh_key = {}, .bind_group_data = 7};
    const pbr::MaterialPipelineKey<CustomMaterial> same{.mesh_key = {}, .bind_group_data = 7};
    EXPECT_TRUE(key == same);
    // 3, not 2: the mesh pipeline's layout is `[view, view binding arrays, mesh]`, so the
    // mesh uniform owns group 2 (`mesh_bindings.wgsl:7`) and the material's layout is
    // inserted after it (Bevy `MATERIAL_BIND_GROUP_INDEX`, `material.rs:66`). Asserting 2
    // here is what let the material's layout displace the mesh's and fail pipeline creation.
    EXPECT_EQ(pbr::MATERIAL_BIND_GROUP_INDEX, 3u) << "Bevy's MATERIAL_BIND_GROUP_INDEX";
}

/** @brief The prepared-material properties default the way Bevy's do (Bevy
 *  `MaterialProperties`, `bevy_pbr/src/material.rs:1510-1585`).
 *
 *  These are the values the pipeline reads for every material that overrides
 *  nothing, so a wrong default here changes how an entire class of materials is
 *  drawn - the same reason the trait's own defaults are pinned one test above. */
TEST(MaterialTrait, PreparedPropertiesDefaultToForwardOpaque) {
    const pbr::MaterialProperties properties;
    EXPECT_EQ(properties.render_method, pbr::OpaqueRendererMethod::Forward);
    EXPECT_EQ(properties.alpha_mode, render::AlphaMode::opaque());
    EXPECT_EQ(properties.render_phase_type, pbr::RenderPhaseType::Opaque);
    EXPECT_FLOAT_EQ(properties.depth_bias, 0.0f);
    EXPECT_FALSE(properties.reads_view_transmission_texture) << "a field on the properties, not the trait's accessor";
    EXPECT_FALSE(properties.bindless) << "bindless is decided from platform support, not assumed";
    EXPECT_TRUE(properties.draw_functions.empty());
    EXPECT_TRUE(properties.shaders.empty());

    // The prepared material ties a binding to those properties.
    auto shared = std::make_shared<pbr::MaterialProperties>();
    const pbr::PreparedMaterial prepared{.binding = pbr::MaterialBindingId{}, .properties = shared};
    ASSERT_TRUE(prepared.properties != nullptr);
    EXPECT_EQ(prepared.properties.get(), shared.get()) << "the properties are shared, as Bevy shares them in an Arc";
}

/** @brief The material pipeline cache is non-generic and starts empty (Bevy
 *  `SpecializedMaterialPipelineCache` and `SpecializedMaterialViewPipelineCache`,
 *  `material.rs:952-967`).
 *
 *  Bevy 0.18's 3D cache holds no material type where the 2D counterpart is generic
 *  over it - the ids it stores are already specialized - and it is keyed by the
 *  retained view and then by the material's main entity, holding each entity's
 *  pipeline id with the tick it was specialized at. */
TEST(MaterialTrait, PipelineCacheIsNonGenericAndKeyedPerEntity) {
    pbr::SpecializedMaterialPipelineCache cache;
    EXPECT_TRUE(cache.map.empty()) << "a fresh cache holds no view";

    pbr::SpecializedMaterialViewPipelineCache view_cache;
    EXPECT_TRUE(view_cache.map.empty());
    EXPECT_TRUE((std::is_same_v<decltype(view_cache.map),
                                render::sync_world::MainEntityHashMap<std::pair<ecs::Tick, render::CachedRenderPipelineId>>>))
        << "an entity maps to its pipeline id and the tick it was specialized at";

    view_cache.map[render::sync_world::MainEntity{ecs::Entity::from_index(7)}] = {ecs::Tick{3},
                                                                                render::CachedRenderPipelineId{9}};
    ASSERT_EQ(view_cache.map.size(), 1u);
    EXPECT_EQ(view_cache.map.begin()->second.first.get(), 3u);
}

/** @brief The erased material key keeps the payload's type, hash and equality
 *  (Bevy `ErasedMaterialKey`, `material.rs:1430-1445`).
 *
 *  One specialization cache serves every material type, so this is what keeps two
 *  types with equal bytes apart: the type is part of the key, not only the value.
 *  Bevy stores the `TypeId`, a precomputed hash and the boxed value; a key that got
 *  any of the three wrong would silently share a pipeline between materials. */
TEST(MaterialTrait, ErasedKeyComparesTypeAndValue) {
    const pbr::ErasedMaterialKey same_a{7};
    const pbr::ErasedMaterialKey same_b{7};
    const pbr::ErasedMaterialKey other_value{8};
    const pbr::ErasedMaterialKey other_type{7u};

    EXPECT_EQ(same_a, same_b);
    EXPECT_EQ(same_a.hash(), same_b.hash()) << "the hash is precomputed from the value, as Bevy's is";
    EXPECT_NE(same_a, other_value);
    EXPECT_NE(same_a, other_type) << "same numeric value, different type: still a different key";
    EXPECT_TRUE(same_a.type_id() == same_b.type_id());
    EXPECT_FALSE(same_a.type_id() == other_type.type_id());
}

/** @brief The erased pipeline key carries the mesh half, the erased material key and
 *  the material type (Bevy `ErasedMaterialPipelineKey`, `material.rs:438-443`).
 *
 *  The two are not redundant and this pins that: `material_key` identifies the
 *  bind-group data a material family specializes on, `type_id` the material type it
 *  belongs to. A key that dropped either would send a material to a pipeline built
 *  for another. */
TEST(MaterialTrait, ErasedPipelineKeyComparesAllThreeParts) {
    const pbr::ErasedMaterialKey material_key{11};
    const pbr::ErasedMaterialKey other_material_key{12};

    const pbr::ErasedMaterialPipelineKey base{
        .mesh_key = {}, .material_key = material_key, .type_id = epix::meta::type_id<PlainMaterial>()};
    const pbr::ErasedMaterialPipelineKey same{
        .mesh_key = {}, .material_key = material_key, .type_id = epix::meta::type_id<PlainMaterial>()};
    const pbr::ErasedMaterialPipelineKey other_material{
        .mesh_key = {}, .material_key = other_material_key, .type_id = epix::meta::type_id<PlainMaterial>()};
    const pbr::ErasedMaterialPipelineKey other_type{
        .mesh_key = {}, .material_key = material_key, .type_id = epix::meta::type_id<CustomMaterial>()};

    EXPECT_EQ(base, same);
    EXPECT_EQ(std::hash<pbr::ErasedMaterialPipelineKey>{}(base),
              std::hash<pbr::ErasedMaterialPipelineKey>{}(same));
    EXPECT_NE(base, other_material) << "the erased bind-group data is part of the key";
    EXPECT_NE(base, other_type) << "so is the material type, which Bevy keeps beside it";
}

/** @brief A prepared material's shader list is read back by label (Bevy
 *  `MaterialProperties::get_shader`, `material.rs:1478-1490`).
 *
 *  The specialization applies these overrides, so a lookup that missed - or that
 *  answered with the wrong slot - would silently specialize a material against the
 *  default mesh shader. The list holds one entry per label, and the labels are
 *  distinct types, so asking for the other one must answer nothing. */
TEST(MaterialTrait, ShaderLookupIsByLabel) {
    const auto vertex   = epix::assets::Handle<epix::shader::Shader>{
        epix::assets::AssetId<epix::shader::Shader>::invalid()};
    pbr::MaterialProperties properties;
    properties.shaders.emplace_back(
        render::label::intern(render::phase::ShaderLabel{pbr::MaterialVertexShader{}}), vertex);

    ASSERT_NE(properties.get_shader<pbr::MaterialVertexShader>(), nullptr);
    EXPECT_TRUE(*properties.get_shader<pbr::MaterialVertexShader>() == vertex);
    EXPECT_EQ(properties.get_shader<pbr::MaterialFragmentShader>(), nullptr)
        << "a label with no entry answers nothing rather than the first shader in the list";
}

/** @brief The material specializer is usable by the mesh specialization cache (Bevy
 *  `SpecializedMeshPipeline for MaterialPipelineSpecializer`, `material.rs:456`).
 *
 *  The cache keys on `S::Key` and calls `specialize(key, layout)`, so this is what
 *  makes the erased specializer usable at all: a key type or signature that drifted
 *  would fail here rather than in a system no test drives. Checked at compile time,
 *  since the pipeline it produces needs a device. */
static_assert(render::SpecializedMeshPipeline<pbr::MaterialPipelineSpecializer>);
static_assert(std::is_same_v<pbr::MaterialPipelineSpecializer::Key, pbr::ErasedMaterialPipelineKey>);

/** @brief The non-bindless material allocator's index bookkeeping (Bevy
 *  `MaterialBindGroupNonBindlessAllocator`, `material_bind_groups.rs:177-190` and
 *  `:1833-1901`).
 *
 *  One bind group per material, always at slot 0, over a list that grows and shrinks.
 *  The invariants pinned here are the ones a re-implementation gets wrong: a freed
 *  index is reused before the list grows, a freed group stops waiting to be prepared,
 *  and a group holding only an unprepared bind group answers with a slab that is not
 *  yet usable rather than one that is. */
TEST(MaterialBindGroupAllocator, NonBindlessAllocatesReusesAndFrees) {
    pbr::MaterialBindGroupNonBindlessAllocator allocator{"test material"};
    EXPECT_EQ(allocator.len(), 0u);

    const auto first = allocator.allocate_unprepared(render::render_resource::UnpreparedBindGroup{},
                                                     render::BindGroupLayoutDescriptor{});
    EXPECT_EQ(first.group.index, 0u);
    EXPECT_EQ(first.slot.slot, 0u) << "a non-bindless material always lives in slot 0";
    EXPECT_EQ(allocator.len(), 1u);
    EXPECT_TRUE(allocator.to_prepare().contains(first.group)) << "an unprepared group has to be prepared later";

    const auto second = allocator.allocate_unprepared(render::render_resource::UnpreparedBindGroup{},
                                                      render::BindGroupLayoutDescriptor{});
    EXPECT_EQ(second.group.index, 1u) << "a second group grows the list";

    allocator.free(first);
    EXPECT_FALSE(allocator.to_prepare().contains(first.group))
        << "preparing a freed group would prepare an allocation that is gone";
    EXPECT_FALSE(allocator.get(first.group).has_value());

    const auto reused = allocator.allocate_unprepared(render::render_resource::UnpreparedBindGroup{},
                                                      render::BindGroupLayoutDescriptor{});
    EXPECT_EQ(reused.group.index, 0u) << "the freed index is reused before the list grows";
    EXPECT_EQ(allocator.len(), 2u) << "and reusing it did not grow the list";
    EXPECT_TRUE(allocator.free_indices().empty());

    const auto slab = allocator.get(reused.group);
    ASSERT_TRUE(slab.has_value());
    EXPECT_TRUE(std::holds_alternative<std::monostate>(*slab))
        << "unprepared, so there is no bind group to draw with yet";
    EXPECT_THROW((void)allocator.get(pbr::MaterialBindGroupIndex{9}), std::out_of_range)
        << "an index the list does not cover is the panic Bevy's indexing would be";
}

namespace {
// Attaching RenderPlugin loads an embedded shader, which spawns an async load task
// that terminates the process when the IO task pool has not been initialized; the
// same guard the other device-facing pbr tests use.
struct IoTaskPoolInit {
    IoTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPool{epix::task::TaskPoolBuilder{}.num_threads(4)});
    }
} g_io_task_pool_init;
}  // namespace

/** @brief Preparing an allocator turns its unprepared groups into usable ones (Bevy
 *  `prepare_bind_groups`, `material_bind_groups.rs:1909-1975`).
 *
 *  This is the device-facing half, so it needs a real device: the `Data` binding is
 *  packed into a uniform buffer, the group is created through the layout the pipeline
 *  server holds, and the slab stops reporting itself as unprepared - which is the
 *  state the draw path checks before it binds anything. */
TEST(MaterialBindGroupAllocator, PrepareCreatesTheBindGroupFromData) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::shader::ShaderPlugin{});
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    auto& world = render_app->get().world_mut();

    const auto device = world.get_resource<wgpu::Device>();
    const auto queue  = world.get_resource<wgpu::Queue>();
    const auto server = world.get_resource<render::PipelineServer>();
    ASSERT_TRUE(device.has_value());
    ASSERT_TRUE(queue.has_value());
    ASSERT_TRUE(server.has_value());

    // A layout with one uniform buffer at binding 0, which is what a `Data` binding is
    // materialized into.
    const std::array<wgpu::BindGroupLayoutEntry, 1> layout_entries{
        wgpu::BindGroupLayoutEntry()
            .setBinding(0)
            .setVisibility(wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment)
            .setBuffer(wgpu::BufferBindingLayout()
                           .setType(wgpu::BufferBindingType::eUniform)
                           .setHasDynamicOffset(false)
                           .setMinBindingSize(16))};
    render::BindGroupLayoutDescriptor layout =
        render::BindGroupLayoutDescriptor().set_label("test material layout").set_entries(layout_entries);

    render::render_resource::BindingResources bindings;
    bindings.push_back(0, render::render_resource::BindingResources::data(
                              render::render_resource::OwnedData{std::vector<std::byte>(16)}));

    pbr::MaterialBindGroupNonBindlessAllocator allocator{"test material"};
    const auto id = allocator.allocate_unprepared(
        render::render_resource::UnpreparedBindGroup{.bindings = std::move(bindings)}, std::move(layout));
    EXPECT_EQ(allocator.to_prepare().size(), 1u);

    allocator.prepare_bind_groups(device->get(), queue->get(), server->get());

    EXPECT_TRUE(allocator.to_prepare().empty()) << "a prepared group is no longer waiting";
    const auto slab = allocator.get(id.group);
    ASSERT_TRUE(slab.has_value());
    EXPECT_TRUE(std::holds_alternative<std::reference_wrapper<const render::render_resource::PreparedBindGroup>>(*slab))
        << "the slab is usable once prepared, which is what the draw path checks";
}

/** @brief The material data buffer's slot allocation (Bevy `MaterialDataBuffer`,
 *  `bevy_pbr/src/material_bind_groups.rs:361-376` and `:2017-2049`).
 *
 *  `#[data]` bindings are not GPU resources of their own, so the allocator places each
 *  one in a slot of a shared untyped buffer. The arithmetic is what matters: a slot
 *  starts `slot * aligned_element_size` bytes in, a freed slot is reused before the
 *  buffer grows, and a reused slot's bytes are overwritten where they are. Getting any
 *  of those wrong hands the shader another material's data instead of failing. */
TEST(MaterialBindGroupAllocator, DataBufferSlotsAreReusedAndWrittenInPlace) {
    pbr::MaterialDataBuffer data{render::render_resource::BindingNumber{5}, /*aligned_size=*/4};
    EXPECT_EQ(data.len, 0u);

    const std::vector<std::uint8_t> first{1, 2, 3, 4};
    const std::uint32_t slot0 = data.insert(first);
    EXPECT_EQ(slot0, 0u);
    EXPECT_EQ(data.len, 1u);
    ASSERT_EQ(data.buffer.values().size(), 4u);
    EXPECT_EQ(data.buffer.values()[0], 1u);
    EXPECT_EQ(data.buffer.dirty, pbr::BufferDirtyState::NeedsReserve) << "a write makes the GPU copy stale";

    const std::vector<std::uint8_t> second{5, 6, 7, 8};
    EXPECT_EQ(data.insert(second), 1u);
    EXPECT_EQ(data.buffer.values().size(), 8u);
    EXPECT_EQ(data.buffer.values()[4], 5u) << "the second element starts one element along, not one byte";

    data.remove(slot0);
    const std::vector<std::uint8_t> third{9, 10, 11, 12};
    EXPECT_EQ(data.insert(third), 0u) << "the freed slot is reused before the buffer grows";
    EXPECT_EQ(data.buffer.values().size(), 8u) << "and reusing it did not grow the buffer";
    EXPECT_EQ(data.buffer.values()[0], 9u) << "and its bytes are overwritten in place";
}

/** @brief The retained buffer's lazy-update state machine (Bevy `RetainedRawBufferVec`,
 *  `material_bind_groups.rs:736-764`), on a real device.
 *
 *  The states are the point: a new buffer needs uploading but not reserving, a buffer
 *  whose contents changed needs reserving first, and only then is it clean again. A
 *  machine that skipped the reserve would upload into a buffer too small for its
 *  contents, which fails at the draw rather than at the write. */
TEST(MaterialBindGroupAllocator, RetainedBufferReservesThenUploads) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::shader::ShaderPlugin{});
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    auto& world = render_app->get().world_mut();
    const auto device = world.get_resource<wgpu::Device>();
    const auto queue  = world.get_resource<wgpu::Queue>();
    ASSERT_TRUE(device.has_value());
    ASSERT_TRUE(queue.has_value());

    pbr::RetainedRawBufferVec<std::uint32_t> retained{wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst};
    EXPECT_EQ(retained.dirty, pbr::BufferDirtyState::NeedsUpload)
        << "a new buffer has nothing to reserve, but its contents are not written either";

    retained.push(7u);
    retained.prepare(device->get());
    EXPECT_EQ(retained.dirty, pbr::BufferDirtyState::NeedsUpload)
        << "prepare only recreates the buffer, so the contents are still to be written";
    retained.write(device->get(), queue->get());
    EXPECT_EQ(retained.dirty, pbr::BufferDirtyState::Clean);
    EXPECT_NE(retained.gpu_buffer(), nullptr) << "writing creates the GPU buffer";

    // What a caller that changed the contents sets, as the index table does.
    retained.dirty = pbr::BufferDirtyState::NeedsReserve;
    retained.prepare(device->get());
    EXPECT_EQ(retained.dirty, pbr::BufferDirtyState::NeedsUpload) << "reserving moves it along, it does not finish it";
    retained.write(device->get(), queue->get());
    EXPECT_EQ(retained.dirty, pbr::BufferDirtyState::Clean);
}

/** @brief The bindless index table: a slice per material mapping bindless indices to
 *  binding-array slots (Bevy `MaterialBindlessIndexTable`, `material_bind_groups.rs:122-135`
 *  and `:639-727`).
 *
 *  The shader binary-searches these entries, and every material reads its own slice, so
 *  the two things worth pinning are the offset arithmetic and that one material's write
 *  cannot land in another's slice. */
TEST(MaterialBindGroupAllocator, IndexTableMapsBindlessIndicesToSlotsPerMaterial) {
    const render::render_resource::BindlessIndexTableDescriptor descriptor{
        .indices = {.start = {4}, .end = {7}}, .binding_number = render::render_resource::BindingNumber{0}};
    pbr::MaterialBindlessIndexTable table{descriptor};

    EXPECT_EQ(table.table_length(), 3u);
    EXPECT_EQ(table.get(pbr::MaterialBindGroupSlot{0}).size(), 3u) << "one slice per material slot";
    EXPECT_EQ(table.get_binding(pbr::MaterialBindGroupSlot{0}, {4}), 0u) << "a fresh table is all zeros";
    EXPECT_EQ(table.get_binding(pbr::MaterialBindGroupSlot{0}, {7}), std::nullopt)
        << "the range is half-open, so index 7 is outside a 4..7 table";

    // One material's mapping, deliberately out of index order so the arithmetic is what
    // decides where each lands.
    table.set(pbr::MaterialBindGroupSlot{0}, {{render::render_resource::BindlessIndex{5}, 2u},
                                              {render::render_resource::BindlessIndex{4}, 9u}});
    EXPECT_EQ(table.get_binding(pbr::MaterialBindGroupSlot{0}, {4}), 9u)
        << "index 4 is the first entry of a 4..7 table, whatever order it was written in";
    EXPECT_EQ(table.get_binding(pbr::MaterialBindGroupSlot{0}, {5}), 2u);
    EXPECT_EQ(table.get_binding(pbr::MaterialBindGroupSlot{0}, {6}), 0u) << "an untouched entry stays zero";
    EXPECT_EQ(table.buffer.dirty, pbr::BufferDirtyState::NeedsReserve)
        << "a written table has to be recreated and uploaded";

    table.set(pbr::MaterialBindGroupSlot{1}, {{render::render_resource::BindlessIndex{6}, 7u}});
    EXPECT_EQ(table.get_binding(pbr::MaterialBindGroupSlot{1}, {6}), 7u);
    EXPECT_EQ(table.get_binding(pbr::MaterialBindGroupSlot{0}, {6}), 0u)
        << "the second material's write must not land in the first material's slice";
    EXPECT_EQ(table.buffer.values().size(), 6u) << "two slices of three entries";
}

/** @brief A bindless resource's identity is its handle (Bevy `BindingResourceId`,
 *  `material_bind_groups.rs:228-240`), on a real device.
 *
 *  A binding array finds a resource it already holds through this key, so the identity
 *  decides whether two materials share one array slot or take two: a collision would
 *  silently point the second material at the first material's resource, and an identity
 *  that never matched would spend a slot on a duplicate of the same texture. That is why
 *  this is checked through the map the array actually uses, not just with `==`. */
TEST(MaterialBindGroupAllocator, ResourceIdsCompareAndHashByHandle) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::shader::ShaderPlugin{});
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    const auto device = render_app->get().world().get_resource<wgpu::Device>();
    ASSERT_TRUE(device.has_value());

    const wgpu::BufferDescriptor buffer_descriptor =
        wgpu::BufferDescriptor().setSize(16).setUsage(wgpu::BufferUsage::eUniform | wgpu::BufferUsage::eCopyDst);
    const auto first  = device->get().createBuffer(buffer_descriptor);
    const auto second = device->get().createBuffer(buffer_descriptor);

    const pbr::BindingResourceId first_id{pbr::BindingResourceId::Buffer{first}};
    const pbr::BindingResourceId first_again{pbr::BindingResourceId::Buffer{first}};
    const pbr::BindingResourceId second_id{pbr::BindingResourceId::Buffer{second}};

    EXPECT_EQ(first_id, first_again) << "the same handle names the same resource";
    EXPECT_EQ(std::hash<pbr::BindingResourceId>{}(first_id), std::hash<pbr::BindingResourceId>{}(first_again))
        << "equal identities have to hash equally, or the lookup that finds them misses";
    EXPECT_NE(first_id, second_id);
    EXPECT_NE(first_id, pbr::BindingResourceId{pbr::BindingResourceId::DataBuffer{}})
        << "the kind is part of the identity, not only the handle";

    std::unordered_map<pbr::BindingResourceId, std::uint32_t> slots;
    slots.emplace(first_id, 0);
    slots.emplace(second_id, 1);
    slots.emplace(first_again, 2);
    EXPECT_EQ(slots.size(), 2u) << "one resource cannot hold two slots";
    EXPECT_EQ(slots.at(first_again), 0u) << "and the first insert is the one that stands";
}

/** @brief The allocator dispatch is per material type (Bevy `MaterialBindGroupAllocators`
 *  and `MaterialBindGroupAllocator`, `material_bind_groups.rs:36`, `:44-49`).
 *
 *  Each material type decides for itself whether it is bindless, so the choice is stored
 *  per type rather than once for all materials. A single shared allocator would put a
 *  bindless material's resources through the non-bindless path - one bind group per
 *  material, slot 0 - which is not a slower version of the same thing but a different
 *  layout the shader does not read. */
TEST(MaterialBindGroupAllocator, DispatchIsKeyedByMaterialType) {
    pbr::MaterialBindGroupAllocators allocators;
    allocators.allocators.emplace(
        epix::meta::type_id<PlainMaterial>(),
        pbr::MaterialBindGroupAllocator{pbr::MaterialBindGroupNonBindlessAllocator{"plain material"}});
    allocators.allocators.emplace(epix::meta::type_id<CustomMaterial>(),
                                  pbr::MaterialBindGroupAllocator{pbr::MaterialBindGroupBindlessAllocator{}});

    ASSERT_EQ(allocators.allocators.size(), 2u) << "one allocator per material type";
    const auto& plain = allocators.allocators.at(epix::meta::type_id<PlainMaterial>()).value;
    const auto& custom = allocators.allocators.at(epix::meta::type_id<CustomMaterial>()).value;
    EXPECT_TRUE(std::holds_alternative<pbr::MaterialBindGroupNonBindlessAllocator>(plain))
        << "a material without a bindless descriptor gets the non-bindless allocator";
    EXPECT_TRUE(std::holds_alternative<pbr::MaterialBindGroupBindlessAllocator>(custom));
    EXPECT_EQ(std::get<pbr::MaterialBindGroupNonBindlessAllocator>(plain).label(), "plain material")
        << "and each carries its own label, which is what its bind groups are created under";
}

/** @brief Constructing the bindless allocator and choosing between the two (Bevy
 *  `MaterialBindGroupBindlessAllocator::new` and `MaterialBindGroupAllocator::new`,
 *  `material_bind_groups.rs:770-806` and `:458-478`), on a real device.
 *
 *  The dispatch is the decision that decides which layout a material's bind groups use,
 *  and the fallback buffers are what an unused slot in a binding array points at - a
 *  missing one is a hole the shader reads. Both need a device, which is why this is a
 *  GPU test. */
TEST(MaterialBindGroupAllocator, BindlessConstructionBuildsFallbackBuffersAndChoosesByDescriptor) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::shader::ShaderPlugin{});
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    const auto device = render_app->get().world().get_resource<wgpu::Device>();
    ASSERT_TRUE(device.has_value());

    render::render_resource::BindlessDescriptor descriptor;
    descriptor.buffers.push_back(render::render_resource::BindlessBufferDescriptor{
        .binding_number = render::render_resource::BindingNumber{4},
        .bindless_index = render::render_resource::BindlessIndex{2},
        .size           = 64});
    const render::render_resource::BindlessSlabResourceLimit limit{.custom = 32u};

    const auto allocator = pbr::MaterialBindGroupAllocator::make(
        device->get(), "bindless material", descriptor, render::BindGroupLayoutDescriptor{}, limit);
    ASSERT_TRUE(std::holds_alternative<pbr::MaterialBindGroupBindlessAllocator>(allocator.value))
        << "a material with a bindless descriptor gets the bindless allocator";

    const auto& bindless = std::get<pbr::MaterialBindGroupBindlessAllocator>(allocator.value);
    EXPECT_EQ(bindless.slab_capacity, 32u) << "the capacity comes from the material's limit, not a default";
    EXPECT_TRUE(bindless.slabs.empty()) << "slabs are created on demand, not up front";
    ASSERT_EQ(bindless.fallback_buffers.size(), 1u) << "one fallback per bindless buffer";
    EXPECT_EQ(bindless.fallback_buffers.at(render::render_resource::BindlessIndex{2}).getSize(), 64u)
        << "sized by the descriptor when it gives a size";

    // And the other arm: a material with no bindless descriptor never builds one.
    const auto plain = pbr::MaterialBindGroupAllocator::make(
        device->get(), "plain material", std::nullopt, render::BindGroupLayoutDescriptor{}, std::nullopt);
    EXPECT_TRUE(std::holds_alternative<pbr::MaterialBindGroupNonBindlessAllocator>(plain.value));
}

/** @brief A binding array shares slots by resource identity (Bevy
 *  `MaterialBindlessBindingArray`, `material_bind_groups.rs:1578-1658`), on a real device.
 *
 *  This is what makes a bindless array worth having: two materials using the same texture
 *  take one slot rather than two, and the slot is released only when the last of them
 *  stops using it. A reference count that went wrong either frees a resource another
 *  material is still drawing with, or leaks a slot the array can never reuse. */
TEST(MaterialBindGroupAllocator, BindingArraySharesSlotsByResourceIdentity) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::shader::ShaderPlugin{});
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    const auto device = render_app->get().world().get_resource<wgpu::Device>();
    ASSERT_TRUE(device.has_value());

    const wgpu::BufferDescriptor buffer_descriptor =
        wgpu::BufferDescriptor().setSize(16).setUsage(wgpu::BufferUsage::eUniform | wgpu::BufferUsage::eCopyDst);
    const auto first  = device->get().createBuffer(buffer_descriptor);
    const auto second = device->get().createBuffer(buffer_descriptor);
    const auto third  = device->get().createBuffer(buffer_descriptor);

    using Array = pbr::MaterialBindlessBindingArray<wgpu::Buffer>;
    Array array = Array::make(render::render_resource::BindingNumber{1},
                              render::render_resource::BindlessResourceType::Buffer);

    const auto resource_type = render::render_resource::BindlessResourceType::Buffer;
    const auto first_id      = pbr::binding_resource_id(first, resource_type);
    const auto second_id     = pbr::binding_resource_id(second, resource_type);
    const auto third_id      = pbr::binding_resource_id(third, resource_type);

    EXPECT_EQ(array.insert(first_id, first), 0u);
    EXPECT_EQ(array.insert(first_id, first), 0u) << "the same resource takes one slot, not two";
    EXPECT_EQ(array.len, 1u) << "and the second use is a reference, not an allocation";
    EXPECT_EQ(array.bindings.at(0)->ref_count, 2u) << "which is what the count has to say";

    EXPECT_EQ(array.insert(second_id, second), 1u);
    EXPECT_EQ(array.len, 2u);
    EXPECT_EQ(array.find(first_id), 0u);
    EXPECT_EQ(array.find(third_id), std::nullopt) << "a resource the array does not hold has no slot";

    EXPECT_FALSE(array.remove(0)) << "the first material's reference is not the last one";
    EXPECT_EQ(array.len, 2u) << "`len` counts allocated slots, not references: a slot with a user left is still allocated";
    EXPECT_EQ(array.find(first_id), 0u) << "the resource stays while a material still uses it";
    EXPECT_TRUE(array.remove(0)) << "the second reference is the one that frees it";
    EXPECT_EQ(array.find(first_id), std::nullopt);
    EXPECT_EQ(array.len, 1u) << "and only the freed slot goes away";

    EXPECT_EQ(array.insert(third_id, third), 0u) << "the freed slot comes back before a new one is made";
    EXPECT_EQ(array.bindings.size(), 2u) << "and reusing it did not grow the array past the second slot";
}

/** @brief The slab's construction and allocation walk (Bevy `MaterialBindlessSlab::new`,
 *  `check_allocation` and `insert_resources`, `material_bind_groups.rs:1695-1792`,
 *  `:962-1039` and `:1045-1134`), on a real device.
 *
 *  Two materials of the same type share the buffer they both bind, so the second needs no
 *  free slot, has to be placed in the same slot the first used - Bevy asserts that - and
 *  must not count as a newly allocated resource. That count is what decides when a new
 *  slab is needed, so counting a reuse would fill slabs early and start bind groups for
 *  no reason. */
TEST(MaterialBindGroupAllocator, SlabBuildsArraysAndSharesResourcesAcrossMaterials) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::shader::ShaderPlugin{});
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    const auto device = render_app->get().world().get_resource<wgpu::Device>();
    ASSERT_TRUE(device.has_value());

    using render::render_resource::BindlessIndex;
    using render::render_resource::BindlessResourceType;

    const auto buffer_descriptor = [](render::render_resource::BindingNumber binding, BindlessIndex index,
                                      std::optional<std::size_t> size) {
        return render::render_resource::BindlessBufferDescriptor{
            .binding_number = binding, .bindless_index = index, .size = size};
    };

    render::render_resource::BindlessDescriptor descriptor;
    descriptor.resources = {BindlessResourceType::Buffer, BindlessResourceType::DataBuffer};
    descriptor.buffers.push_back(
        buffer_descriptor(render::render_resource::BindingNumber{2}, BindlessIndex{0}, std::nullopt));
    descriptor.buffers.push_back(buffer_descriptor(render::render_resource::BindingNumber{3}, BindlessIndex{1}, 16));
    descriptor.index_tables.push_back(render::render_resource::BindlessIndexTableDescriptor{
        .indices        = {.start = BindlessIndex{0}, .end = BindlessIndex{2}},
        .binding_number = render::render_resource::BindingNumber{0}});

    pbr::MaterialBindlessSlab slab = pbr::MaterialBindlessSlab::make(descriptor);
    ASSERT_EQ(slab.buffers.size(), 1u) << "one array per bindless buffer, keyed by its bindless index";
    ASSERT_EQ(slab.data_buffers.size(), 1u) << "plain old data gets a buffer, not a binding array";
    ASSERT_EQ(slab.bindless_index_tables.size(), 1u);
    EXPECT_FALSE(slab.bind_group.has_value()) << "a new slab has no bind group until it is prepared";

    const auto buffer = device->get().createBuffer(
        wgpu::BufferDescriptor().setSize(16).setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst));

    // Two materials of the same type: the same buffer, each with its own data.
    const auto make_material = [&buffer](std::uint8_t tag) {
        render::render_resource::BindingResources bindings;
        bindings.push_back(0, render::render_resource::BindingResources::buffer(buffer));
        bindings.push_back(1, render::render_resource::BindingResources::data(
                                  render::render_resource::OwnedData{std::vector<std::byte>(16, std::byte{tag})}));
        return render::render_resource::UnpreparedBindGroup{.bindings = std::move(bindings)};
    };

    auto first                 = make_material(1);
    const auto first_candidate = slab.check_allocation(first);
    ASSERT_TRUE(first_candidate.has_value());
    EXPECT_EQ(first_candidate->needed_free_slots, 1u)
        << "the buffer needs a slot; the data buffer lives in its own buffer and needs none";
    const auto first_slots = slab.insert_resources(std::move(first.bindings), *first_candidate);
    EXPECT_EQ(first_slots.at(BindlessIndex{0}), 0u);
    EXPECT_EQ(slab.allocated_resource_count, 1u) << "only the buffer occupies a bounded array";

    auto second                 = make_material(2);
    const auto second_candidate = slab.check_allocation(second);
    ASSERT_TRUE(second_candidate.has_value());
    EXPECT_EQ(second_candidate->needed_free_slots, 0u) << "the second material reuses the buffer's slot";
    EXPECT_EQ(second_candidate->pre_existing_resources.at(BindlessIndex{0}), 0u);
    slab.insert_resources(std::move(second.bindings), *second_candidate);

    EXPECT_EQ(slab.allocated_resource_count, 1u) << "and reusing a slot is not a new allocation";
    ASSERT_EQ(slab.buffers.at(BindlessIndex{0}).len, 1u) << "one slot for two materials";
    EXPECT_EQ(slab.buffers.at(BindlessIndex{0}).bindings.at(0)->ref_count, 2u) << "held by both of them";
}

/** @brief A slab finds the index table covering a bindless index (Bevy
 *  `get_bindless_index_table`, `material_bind_groups.rs:1558-1575`).
 *
 *  The tables are searched by range, so the three cases that matter are a hit in each
 *  table, a gap between them, and the half-open end. A search that compared only starts
 *  would answer with a neighbouring table instead of nothing, and the shader would read a
 *  table that does not describe its resource. */
TEST(MaterialBindGroupAllocator, SlabFindsTheIndexTableCoveringAnIndex) {
    using render::render_resource::BindlessIndex;
    using render::render_resource::BindlessIndexTableDescriptor;
    using render::render_resource::BindingNumber;

    render::render_resource::BindlessDescriptor descriptor;
    descriptor.index_tables.push_back(
        BindlessIndexTableDescriptor{.indices = {.start = BindlessIndex{0}, .end = BindlessIndex{2}},
                                     .binding_number = BindingNumber{0}});
    descriptor.index_tables.push_back(
        BindlessIndexTableDescriptor{.indices = {.start = BindlessIndex{4}, .end = BindlessIndex{7}},
                                     .binding_number = BindingNumber{1}});

    const pbr::MaterialBindlessSlab slab = pbr::MaterialBindlessSlab::make(descriptor);

    ASSERT_NE(slab.get_bindless_index_table(BindlessIndex{0}), nullptr);
    EXPECT_EQ(slab.get_bindless_index_table(BindlessIndex{1})->binding_number.number, 0u)
        << "the first table covers 0..2";
    ASSERT_NE(slab.get_bindless_index_table(BindlessIndex{5}), nullptr);
    EXPECT_EQ(slab.get_bindless_index_table(BindlessIndex{6})->binding_number.number, 1u)
        << "the second covers 4..7";
    EXPECT_EQ(slab.get_bindless_index_table(BindlessIndex{2}), nullptr) << "index 2 is between the tables";
    EXPECT_EQ(slab.get_bindless_index_table(BindlessIndex{3}), nullptr);
    EXPECT_EQ(slab.get_bindless_index_table(BindlessIndex{7}), nullptr) << "and the ranges are half-open";
}

/** @brief The dispatch's `get` reaches whichever allocator holds the group (Bevy
 *  `MaterialBindGroupAllocator::get`, `material_bind_groups.rs:481-492`), on a real device.
 *
 *  This is the single place the two allocators are used through one interface, and the
 *  bind group it hands back is what the draw path binds: nothing means the slab is not
 *  prepared yet, so a lookup that answered with a slab from the wrong allocator - or with
 *  an empty one - would surface as a draw with no material data rather than as an error. */
TEST(MaterialBindGroupAllocator, DispatchGetReachesTheAllocatorHoldingTheGroup) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::shader::ShaderPlugin{});
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    auto& world       = render_app->get().world_mut();
    const auto device = world.get_resource<wgpu::Device>();
    const auto queue  = world.get_resource<wgpu::Queue>();
    const auto server = world.get_resource<render::PipelineServer>();
    ASSERT_TRUE(device.has_value());
    ASSERT_TRUE(queue.has_value());
    ASSERT_TRUE(server.has_value());

    const std::array<wgpu::BindGroupLayoutEntry, 1> layout_entries{
        wgpu::BindGroupLayoutEntry()
            .setBinding(0)
            .setVisibility(wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment)
            .setBuffer(wgpu::BufferBindingLayout()
                           .setType(wgpu::BufferBindingType::eUniform)
                           .setHasDynamicOffset(false)
                           .setMinBindingSize(16))};
    render::BindGroupLayoutDescriptor layout =
        render::BindGroupLayoutDescriptor().set_label("dispatch get layout").set_entries(layout_entries);

    render::render_resource::BindingResources bindings;
    bindings.push_back(0, render::render_resource::BindingResources::data(
                              render::render_resource::OwnedData{std::vector<std::byte>(16)}));

    pbr::MaterialBindGroupNonBindlessAllocator non_bindless{"test material"};
    const auto id = non_bindless.allocate_unprepared(
        render::render_resource::UnpreparedBindGroup{.bindings = std::move(bindings)}, std::move(layout));
    non_bindless.prepare_bind_groups(device->get(), queue->get(), server->get());

    const pbr::MaterialBindGroupAllocator allocator{pbr::MaterialBindGroupNonBindlessAllocator{
        std::move(non_bindless)}};
    const auto slab = allocator.get(id.group);
    ASSERT_TRUE(slab.has_value()) << "the dispatch reaches the allocator that holds the group";
    EXPECT_NE(slab->bind_group(), nullptr) << "and hands back the group the draw path binds";

    // The two arms differ here, and Bevy's shapes are why: the non-bindless allocator
    // indexes its list, so an uncovered group is the panic its index would be, while the
    // bindless one goes through `Vec::get` and answers nothing.
    EXPECT_THROW((void)allocator.get(pbr::MaterialBindGroupIndex{7}), std::out_of_range)
        << "a non-bindless allocator indexes its list, so an absent group is a panic";
    const pbr::MaterialBindGroupAllocator bindless_allocator{pbr::MaterialBindGroupBindlessAllocator{}};
    EXPECT_FALSE(bindless_allocator.get(pbr::MaterialBindGroupIndex{0}).has_value())
        << "the bindless allocator has no slabs yet, so the same call answers nothing";
}

/** @brief The bindless allocator's slab walk (Bevy
 *  `MaterialBindGroupBindlessAllocator::allocate_unprepared` and the slab's `try_allocate`,
 *  `material_bind_groups.rs:816-848` and `:910-958`), on a real device.
 *
 *  Three behaviours decide how many bind groups a scene costs: the first material opens a
 *  slab, a material whose resources do not fit the current one opens the next, and a
 *  material that reuses resources already there stays where they are. The last is why a
 *  scene full of one texture does not fill slabs. The special case alongside them is
 *  Bevy's: an *empty* slab accepts a material however many resources it needs, so a
 *  platform whose bindless limit cannot hold even one material still places each in a slab
 *  of its own rather than failing. */
TEST(MaterialBindGroupAllocator, BindlessAllocatorWalksSlabsAndCreatesThemOnDemand) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::shader::ShaderPlugin{});
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    const auto device = render_app->get().world().get_resource<wgpu::Device>();
    ASSERT_TRUE(device.has_value());

    using render::render_resource::BindlessIndex;
    using render::render_resource::BindlessResourceType;

    const auto bindless_allocator_for = [&device](std::vector<BindlessResourceType> resources,
                                                  std::uint32_t capacity) {
        render::render_resource::BindlessDescriptor descriptor;
        descriptor.resources = std::move(resources);
        for (std::size_t index = 0; index < descriptor.resources.size(); ++index) {
            if (descriptor.resources[index] != BindlessResourceType::Buffer) continue;
            descriptor.buffers.push_back(render::render_resource::BindlessBufferDescriptor{
                .binding_number = render::render_resource::BindingNumber{static_cast<std::uint32_t>(2 + index)},
                .bindless_index = BindlessIndex{static_cast<std::uint32_t>(index)},
                .size           = std::nullopt});
        }
        auto allocator = pbr::MaterialBindGroupAllocator::make(
            device->get(), "bindless material", std::move(descriptor),
            render::BindGroupLayoutDescriptor{},
            render::render_resource::BindlessSlabResourceLimit{.custom = capacity});
        return std::get<pbr::MaterialBindGroupBindlessAllocator>(std::move(allocator.value));
    };

    const auto buffer_usage =
        wgpu::BufferDescriptor().setSize(16).setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst);
    const auto first_buffer  = device->get().createBuffer(buffer_usage);
    const auto second_buffer = device->get().createBuffer(buffer_usage);

    const auto material_binding = [](std::vector<wgpu::Buffer> buffers) {
        render::render_resource::BindingResources bindings;
        for (std::size_t index = 0; index < buffers.size(); ++index) {
            bindings.push_back(static_cast<std::uint32_t>(index),
                               render::render_resource::BindingResources::buffer(std::move(buffers[index])));
        }
        return render::render_resource::UnpreparedBindGroup{.bindings = std::move(bindings)};
    };

    // One buffer per material, and room for one resource per slab.
    auto allocator = bindless_allocator_for({BindlessResourceType::Buffer}, 1u);

    const auto first = allocator.allocate_unprepared(material_binding({first_buffer}));
    EXPECT_EQ(first.group.index, 0u) << "the first material opens the first slab";
    EXPECT_EQ(first.slot.slot, 0u);
    EXPECT_EQ(allocator.slabs.size(), 1u);

    const auto second = allocator.allocate_unprepared(material_binding({second_buffer}));
    EXPECT_EQ(second.group.index, 1u) << "a buffer that does not fit opens the next slab";
    EXPECT_EQ(allocator.slabs.size(), 2u);

    const auto third = allocator.allocate_unprepared(material_binding({second_buffer}));
    EXPECT_EQ(third.group.index, 1u) << "but a material reusing that buffer stays in the slab holding it";
    EXPECT_EQ(third.slot.slot, 1u) << "in the next slot, not a new group";
    EXPECT_EQ(allocator.slabs.size(), 2u);

    // The special case: two buffers needed, room for one - and an empty slab still accepts.
    auto cramped = bindless_allocator_for({BindlessResourceType::Buffer, BindlessResourceType::Buffer}, 1u);
    const auto oversized = cramped.allocate_unprepared(material_binding({first_buffer, second_buffer}));
    EXPECT_EQ(oversized.group.index, 0u) << "an empty slab takes a material too large for its capacity";
    EXPECT_EQ(cramped.slabs.size(), 1u);

    const auto next = cramped.allocate_unprepared(material_binding({first_buffer, second_buffer}));
    EXPECT_EQ(next.group.index, 1u) << "while a slab that already holds something enforces the capacity";
}

/** @brief Freeing a material releases only its own references (Bevy
 *  `MaterialBindlessSlab::free`, `material_bind_groups.rs:1138-1201`), on a real device.
 *
 *  Two materials binding the same texture share one array slot, so freeing one of them
 *  must not take that texture away from the other - and the slab's occupancy must fall
 *  only when a slot really was freed, since that count is what decides when the next slab
 *  is needed. A free that decremented it early would fill slabs with materials that no
 *  longer exist; one that never decremented would keep the allocator creating them. */
TEST(MaterialBindGroupAllocator, FreeingAMaterialReleasesOnlyItsOwnReferences) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::shader::ShaderPlugin{});
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    const auto device = render_app->get().world().get_resource<wgpu::Device>();
    ASSERT_TRUE(device.has_value());

    using render::render_resource::BindlessIndex;
    using render::render_resource::BindlessResourceType;

    render::render_resource::BindlessDescriptor descriptor;
    descriptor.resources = {BindlessResourceType::Buffer};
    descriptor.buffers.push_back(render::render_resource::BindlessBufferDescriptor{
        .binding_number = render::render_resource::BindingNumber{2},
        .bindless_index = BindlessIndex{0},
        .size           = std::nullopt});
    descriptor.index_tables.push_back(render::render_resource::BindlessIndexTableDescriptor{
        .indices        = {.start = BindlessIndex{0}, .end = BindlessIndex{1}},
        .binding_number = render::render_resource::BindingNumber{0}});

    const auto buffer = device->get().createBuffer(
        wgpu::BufferDescriptor().setSize(16).setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst));

    const auto material = [&buffer]() {
        render::render_resource::BindingResources bindings;
        bindings.push_back(0, render::render_resource::BindingResources::buffer(buffer));
        return render::render_resource::UnpreparedBindGroup{.bindings = std::move(bindings)};
    };

    pbr::MaterialBindlessSlab slab = pbr::MaterialBindlessSlab::make(descriptor);
    const auto first               = slab.try_allocate(material(), /*slot_capacity=*/2u);
    const auto second              = slab.try_allocate(material(), 2u);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->slot, 0u);
    EXPECT_EQ(second->slot, 1u) << "each material gets its own slot, even sharing a buffer";
    ASSERT_EQ(slab.allocated_resource_count, 1u) << "which the buffer occupies once";
    ASSERT_EQ(slab.live_allocation_count, 2u);

    slab.free(*first, descriptor);
    EXPECT_EQ(slab.live_allocation_count, 1u);
    EXPECT_EQ(slab.allocated_resource_count, 1u) << "the buffer is still held by the other material";
    EXPECT_EQ(slab.buffers.at(BindlessIndex{0}).bindings.at(0)->ref_count, 1u);
    EXPECT_EQ(slab.buffers.at(BindlessIndex{0}).len, 1u) << "so its slot stays allocated";
    EXPECT_EQ(slab.free_slots.size(), 1u) << "and only the material's own slot was released";

    slab.free(*second, descriptor);
    EXPECT_EQ(slab.live_allocation_count, 0u);
    EXPECT_EQ(slab.allocated_resource_count, 0u) << "the last reference frees the resource";
    EXPECT_EQ(slab.buffers.at(BindlessIndex{0}).len, 0u);
    EXPECT_EQ(slab.free_slots.size(), 2u) << "and both slots are available again";
}

/** @brief A slab prepares its bind group from its arrays, tables and fallbacks (Bevy
 *  `MaterialBindlessSlab::prepare` and `prepare_bind_group`,
 *  `material_bind_groups.rs:1204-1318`), on a real device.
 *
 *  The group wgpu is asked to create here is validated against a layout declaring the index
 *  table, the nine common arrays and the material's own buffer array and data buffer, so a
 *  missing array, a wrong binding number or an array without elements fails in this test
 *  rather than at a draw. wgpu reports those as uncaptured validation errors while still
 *  returning a group, so the log is part of the evidence. */
TEST(MaterialBindGroupAllocator, SlabPreparesItsBindGroupFromArraysAndFallbacks) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::shader::ShaderPlugin{});
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    auto& world               = render_app->get().world_mut();
    const auto device         = world.get_resource<wgpu::Device>();
    const auto queue          = world.get_resource<wgpu::Queue>();
    const auto server         = world.get_resource<render::PipelineServer>();
    const auto fallback_image = world.get_resource<render::texture::FallbackImage>();
    ASSERT_TRUE(device.has_value());
    ASSERT_TRUE(queue.has_value());
    ASSERT_TRUE(server.has_value());
    ASSERT_TRUE(fallback_image.has_value());

    using render::render_resource::BindlessIndex;
    using render::render_resource::BindlessResourceType;

    // The material's own buffer and data buffer sit clear of the nine common arrays.
    constexpr std::uint32_t kBufferBinding = 10;
    constexpr std::uint32_t kDataBinding   = 11;
    constexpr std::uint32_t kCapacity      = 4;

    render::render_resource::BindlessDescriptor descriptor;
    descriptor.resources = {BindlessResourceType::Buffer, BindlessResourceType::DataBuffer};
    descriptor.buffers.push_back(render::render_resource::BindlessBufferDescriptor{
        .binding_number = render::render_resource::BindingNumber{kBufferBinding},
        .bindless_index = BindlessIndex{0},
        .size           = std::nullopt});
    descriptor.buffers.push_back(render::render_resource::BindlessBufferDescriptor{
        .binding_number = render::render_resource::BindingNumber{kDataBinding},
        .bindless_index = BindlessIndex{1},
        .size           = 16});
    descriptor.index_tables.push_back(render::render_resource::BindlessIndexTableDescriptor{
        .indices        = {.start = BindlessIndex{0}, .end = BindlessIndex{2}},
        .binding_number = render::render_resource::BindingNumber{0}});

    auto layout_entries = render::render_resource::create_bindless_bind_group_layout_entries(
        /*bindless_index_table_length=*/1, kCapacity, render::render_resource::BindingNumber{0});
    layout_entries.push_back(render::render_resource::binding_types::storage_buffer_read_only(false, 16)
                                 .count(kCapacity)
                                 .build(kBufferBinding, wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment));
    layout_entries.push_back(render::render_resource::binding_types::storage_buffer_read_only(false, 16)
                                 .build(kDataBinding, wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment));
    const render::BindGroupLayoutDescriptor layout =
        render::BindGroupLayoutDescriptor().set_label("slab prepare layout").set_entries(layout_entries);

    wgpu::SamplerDescriptor filtering;
    wgpu::SamplerDescriptor non_filtering;
    wgpu::SamplerDescriptor comparison;
    comparison.setCompare(wgpu::CompareFunction::eAlways);
    const pbr::FallbackBindlessResources fallbacks{.filtering_sampler = device->get().createSampler(filtering),
                                                   .non_filtering_sampler = device->get().createSampler(non_filtering),
                                                   .comparison_sampler    = device->get().createSampler(comparison)};

    pbr::MaterialBindlessSlab slab = pbr::MaterialBindlessSlab::make(descriptor);

    const auto buffer = device->get().createBuffer(
        wgpu::BufferDescriptor().setSize(16).setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst));
    render::render_resource::BindingResources bindings;
    bindings.push_back(0, render::render_resource::BindingResources::buffer(buffer));
    bindings.push_back(1, render::render_resource::BindingResources::data(
                              render::render_resource::OwnedData{std::vector<std::byte>(16)}));
    ASSERT_TRUE(slab.try_allocate(render::render_resource::UnpreparedBindGroup{.bindings = std::move(bindings)},
                                  kCapacity)
                    .has_value());

    std::unordered_map<BindlessIndex, wgpu::Buffer> fallback_buffers;
    fallback_buffers.emplace(
        BindlessIndex{0},
        device->get().createBuffer(wgpu::BufferDescriptor().setSize(16).setUsage(wgpu::BufferUsage::eStorage)));

    EXPECT_EQ(slab.get_bind_group(), nullptr) << "a slab has no group until it is prepared";
    slab.write_buffer(device->get(), queue->get());
    slab.prepare(device->get(), server->get(), "test material", layout, fallbacks, fallback_buffers,
                 fallback_image->get(), descriptor, kCapacity);
    EXPECT_NE(slab.get_bind_group(), nullptr)
        << "wgpu validated the entries against the layout, so a missing or empty array fails here";
}

/** @brief The dispatch drives the bindless arm end to end (Bevy
 *  `MaterialBindGroupAllocator`'s `make`, `allocate_unprepared`, `slab_count`, `allocations`,
 *  `write_buffers`, `prepare_bind_groups`, `get` and `free`,
 *  `material_bind_groups.rs:458-634`), on a real device.
 *
 *  This is the interface `prepare_materials` will use, so it is worth pinning as a whole:
 *  the first allocation opens a slab, the occupancy the accessors report follows it, the
 *  group the dispatch hands back is the one wgpu accepted, freeing releases the slab's
 *  reference without discarding the slab, and a prepared bind group is refused because a
 *  bindless material has no say in its own group. */
/** @brief The erased key a material's pipeline is specialized under separates the material
 *  *type* and the material's *data*, and a material with no data has one empty key (Bevy
 *  `ErasedMaterialKey`, `material.rs:1430-1465`, and `ErasedMaterialPipelineKey`, `:438-443`).
 *
 *  This is the part of the specialization the port had no field for until `MaterialProperties`
 *  gained `material_key`: without it, two materials of one type whose data differs would share
 *  a compiled pipeline. Bevy's key has to be hashable and comparable, which is what makes it
 *  usable as a pipeline-cache key - a property this pins. */
TEST(MaterialTrait, TheErasedKeySeparatesMaterialDataAndType) {
    const pbr::ErasedMaterialKey one{pbr::ErasedMaterialKey{1}};
    const pbr::ErasedMaterialKey one_again{pbr::ErasedMaterialKey{1}};
    const pbr::ErasedMaterialKey two{pbr::ErasedMaterialKey{2}};
    // Same value, different C++ type: Bevy's `TypeId` half of the key.
    const pbr::ErasedMaterialKey unsigned_one{pbr::ErasedMaterialKey{1u}};

    EXPECT_EQ(one, one_again) << "the same data under the same type is the same key";
    EXPECT_EQ(one.hash(), one_again.hash());
    EXPECT_NE(one, two) << "different data must not share a pipeline";
    EXPECT_NE(one, unsigned_one) << "and neither must the same value of another type";

    const pbr::ErasedMaterialPipelineKey key_one{.mesh_key     = pbr::MeshPipelineKey{},
                                                 .material_key = one,
                                                 .type_id      = epix::meta::type_id<int>()};
    const pbr::ErasedMaterialPipelineKey mesh_differs{.mesh_key     = pbr::MeshPipelineKey::LIGHTMAPPED,
                                                      .material_key = one,
                                                      .type_id      = epix::meta::type_id<int>()};
    const pbr::ErasedMaterialPipelineKey data_differs{.mesh_key     = pbr::MeshPipelineKey{},
                                                      .material_key = two,
                                                      .type_id      = epix::meta::type_id<int>()};
    EXPECT_EQ(key_one, key_one);
    EXPECT_NE(key_one, mesh_differs) << "the mesh half is part of the key";
    EXPECT_NE(key_one, data_differs) << "and so is the material's data";

    const pbr::MaterialProperties properties;
    EXPECT_EQ(properties.material_key, pbr::ErasedMaterialKey{})
        << "a material with no bind-group data of its own hashes as the empty key, which is "
           "the default the port needs and Bevy has no counterpart for";
}

/** @brief A material's properties carry one draw function per phase label, and a label
 *  nothing registered answers nothing (Bevy `MaterialProperties::draw_functions`,
 *  `material.rs:1533`, and `get_draw_function`, `:1570-1576`).
 *
 *  This is the lookup the queue makes: a miss is what makes it skip a mesh rather than draw
 *  it with the previous item's pipeline, so a label that silently matched another phase's
 *  entry would be worse than no entry at all. */
TEST(MaterialTrait, TheDrawFunctionListIsKeyedByLabel) {
    pbr::MaterialProperties properties;
    EXPECT_FALSE(properties.get_draw_function<pbr::MainPassOpaqueDrawFunction>().has_value())
        << "a material with no prepared properties has no draw function for any phase";

    properties.draw_functions.push_back(
        {epix::render::label::intern(epix::render::phase::DrawFunctionLabel{pbr::MainPassOpaqueDrawFunction{}}),
         epix::render::phase::DrawFunctionId{7}});

    const auto opaque = properties.get_draw_function<pbr::MainPassOpaqueDrawFunction>();
    ASSERT_TRUE(opaque.has_value()) << "the label it was filed under finds it";
    EXPECT_EQ(opaque->get(), 7u);
    EXPECT_FALSE(properties.get_draw_function<pbr::MainPassAlphaMaskDrawFunction>().has_value())
        << "and another phase's label does not";
    EXPECT_FALSE(properties.get_draw_function<pbr::PrepassOpaqueDrawFunction>().has_value())
        << "nor does a prepass label for a main-pass entry";
}

TEST(MaterialBindGroupAllocator, DispatchDrivesTheBindlessArmEndToEnd) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::shader::ShaderPlugin{});
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    auto& world               = render_app->get().world_mut();
    const auto device         = world.get_resource<wgpu::Device>();
    const auto queue          = world.get_resource<wgpu::Queue>();
    const auto server         = world.get_resource<render::PipelineServer>();
    const auto fallback_image = world.get_resource<render::texture::FallbackImage>();
    ASSERT_TRUE(device.has_value());
    ASSERT_TRUE(queue.has_value());
    ASSERT_TRUE(server.has_value());
    ASSERT_TRUE(fallback_image.has_value());

    using render::render_resource::BindlessIndex;
    using render::render_resource::BindlessResourceType;

    constexpr std::uint32_t kBufferBinding = 10;
    constexpr std::uint32_t kDataBinding   = 11;
    constexpr std::uint32_t kCapacity      = 4;

    render::render_resource::BindlessDescriptor descriptor;
    descriptor.resources = {BindlessResourceType::Buffer, BindlessResourceType::DataBuffer};
    descriptor.buffers.push_back(render::render_resource::BindlessBufferDescriptor{
        .binding_number = render::render_resource::BindingNumber{kBufferBinding},
        .bindless_index = BindlessIndex{0},
        .size           = std::nullopt});
    descriptor.buffers.push_back(render::render_resource::BindlessBufferDescriptor{
        .binding_number = render::render_resource::BindingNumber{kDataBinding},
        .bindless_index = BindlessIndex{1},
        .size           = 16});
    descriptor.index_tables.push_back(render::render_resource::BindlessIndexTableDescriptor{
        .indices        = {.start = BindlessIndex{0}, .end = BindlessIndex{2}},
        .binding_number = render::render_resource::BindingNumber{0}});

    auto layout_entries = render::render_resource::create_bindless_bind_group_layout_entries(
        1, kCapacity, render::render_resource::BindingNumber{0});
    layout_entries.push_back(render::render_resource::binding_types::storage_buffer_read_only(false, 16)
                                 .count(kCapacity)
                                 .build(kBufferBinding, wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment));
    layout_entries.push_back(render::render_resource::binding_types::storage_buffer_read_only(false, 16)
                                 .build(kDataBinding, wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment));
    const render::BindGroupLayoutDescriptor layout =
        render::BindGroupLayoutDescriptor().set_label("dispatch bindless layout").set_entries(layout_entries);

    wgpu::SamplerDescriptor filtering;
    wgpu::SamplerDescriptor non_filtering;
    wgpu::SamplerDescriptor comparison;
    comparison.setCompare(wgpu::CompareFunction::eAlways);
    const pbr::FallbackBindlessResources fallbacks{.filtering_sampler = device->get().createSampler(filtering),
                                                   .non_filtering_sampler = device->get().createSampler(non_filtering),
                                                   .comparison_sampler    = device->get().createSampler(comparison)};

    pbr::MaterialBindGroupAllocator allocator = pbr::MaterialBindGroupAllocator::make(
        device->get(), "dispatch bindless material", descriptor, layout,
        render::render_resource::BindlessSlabResourceLimit{.custom = kCapacity});
    EXPECT_EQ(allocator.slab_count(), 0u) << "an allocator starts with no slabs";
    EXPECT_EQ(allocator.allocations(), 0u);

    const auto buffer = device->get().createBuffer(
        wgpu::BufferDescriptor().setSize(16).setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst));
    render::render_resource::BindingResources bindings;
    bindings.push_back(0, render::render_resource::BindingResources::buffer(buffer));
    bindings.push_back(1, render::render_resource::BindingResources::data(
                              render::render_resource::OwnedData{std::vector<std::byte>(16)}));

    const auto id = allocator.allocate_unprepared(
        render::render_resource::UnpreparedBindGroup{.bindings = std::move(bindings)}, layout);
    EXPECT_EQ(id.group.index, 0u) << "the first material opens the first slab";
    EXPECT_EQ(allocator.slab_count(), 1u);
    EXPECT_EQ(allocator.allocations(), 1u);

    allocator.write_buffers(device->get(), queue->get());
    allocator.prepare_bind_groups(device->get(), queue->get(), server->get(), fallbacks, fallback_image->get());

    const auto slab = allocator.get(id.group);
    ASSERT_TRUE(slab.has_value()) << "the dispatch reaches the bindless arm's slab";
    ASSERT_NE(slab->bind_group(), nullptr) << "and wgpu accepted the group it built";

    // Copied out before freeing: a free invalidates the slab's cached group.
    const render::render_resource::PreparedBindGroup prepared{.bind_group = *slab->bind_group(), .bindings = {}};

    allocator.free(id);
    EXPECT_EQ(allocator.allocations(), 0u) << "freeing releases the resources the slab held";
    EXPECT_EQ(allocator.slab_count(), 1u) << "the slab itself stays for the next material";
    EXPECT_EQ(slab->bind_group(), nullptr) << "and its cached group is invalidated";

    EXPECT_THROW(allocator.allocate_prepared(render::render_resource::PreparedBindGroup{prepared}), std::logic_error)
        << "a bindless material has no say in its own bind group, so a prepared one is refused";
}

/** @brief Two stand-in material types: the allocator registry is keyed by material type,
 *  and what the prepare systems have to reach is every key in it. */
struct FirstPreparedMaterial {};
struct SecondPreparedMaterial {};

/** @brief The two `PrepareBindGroups` systems drive *every* registered allocator (Bevy
 *  `prepare_material_bind_groups` and `write_material_bind_group_buffers`,
 *  `material.rs:1907-1937`), which is the half that turns an allocated material into the
 *  bind group its draw binds.
 *
 *  Two allocators under two type keys: a walk that stopped at the first would leave the
 *  second material without a group, which is the failure a registry keyed by type can have
 *  and the one this pins. Bevy runs the pair in the render world's `PrepareBindGroups`;
 *  registering them in `RenderStartup` here drives the same systems through the same
 *  parameter fetch, which is what a windowless test app can run. */
TEST(MaterialBindGroupAllocator, ThePrepareSystemsDriveEveryRegisteredAllocator) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::shader::ShaderPlugin{});
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }
    // The registry and the pair are this plugin's, as they are in Bevy (`material.rs:276-335`).
    app.add_plugins(pbr::MaterialsPlugin{});

    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    auto& render = render_app->get();
    pbr::MaterialBindingId first_id{};
    pbr::MaterialBindingId second_id{};
    {
        auto& world               = render.world_mut();
        const auto device         = world.get_resource<wgpu::Device>();
        const auto queue          = world.get_resource<wgpu::Queue>();
        const auto server         = world.get_resource<render::PipelineServer>();
        const auto fallback_image = world.get_resource<render::texture::FallbackImage>();
        ASSERT_TRUE(device.has_value());
        ASSERT_TRUE(queue.has_value());
        ASSERT_TRUE(server.has_value());
        ASSERT_TRUE(fallback_image.has_value());

        using render::render_resource::BindlessIndex;
        using render::render_resource::BindlessResourceType;

        constexpr std::uint32_t kBufferBinding = 10;
        constexpr std::uint32_t kDataBinding   = 11;
        constexpr std::uint32_t kCapacity      = 2;

        render::render_resource::BindlessDescriptor descriptor;
        descriptor.resources = {BindlessResourceType::Buffer, BindlessResourceType::DataBuffer};
        descriptor.buffers.push_back(render::render_resource::BindlessBufferDescriptor{
            .binding_number = render::render_resource::BindingNumber{kBufferBinding},
            .bindless_index = BindlessIndex{0},
            .size           = std::nullopt});
        descriptor.buffers.push_back(render::render_resource::BindlessBufferDescriptor{
            .binding_number = render::render_resource::BindingNumber{kDataBinding},
            .bindless_index = BindlessIndex{1},
            .size           = 16});
        descriptor.index_tables.push_back(render::render_resource::BindlessIndexTableDescriptor{
            .indices        = {.start = BindlessIndex{0}, .end = BindlessIndex{2}},
            .binding_number = render::render_resource::BindingNumber{0}});

        auto layout_entries = render::render_resource::create_bindless_bind_group_layout_entries(
            1, kCapacity, render::render_resource::BindingNumber{0});
        layout_entries.push_back(render::render_resource::binding_types::storage_buffer_read_only(false, 16)
                                     .count(kCapacity)
                                     .build(kBufferBinding, wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment));
        layout_entries.push_back(render::render_resource::binding_types::storage_buffer_read_only(false, 16)
                                     .build(kDataBinding, wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment));
        const render::BindGroupLayoutDescriptor layout =
            render::BindGroupLayoutDescriptor().set_label("prepare systems layout").set_entries(layout_entries);

        wgpu::SamplerDescriptor filtering;
        wgpu::SamplerDescriptor non_filtering;
        wgpu::SamplerDescriptor comparison;
        comparison.setCompare(wgpu::CompareFunction::eAlways);
        const pbr::FallbackBindlessResources fallbacks{.filtering_sampler = device->get().createSampler(filtering),
                                                       .non_filtering_sampler = device->get().createSampler(non_filtering),
                                                       .comparison_sampler = device->get().createSampler(comparison)};
        // The samplers a real app gets from `init_fallback_bindless_resources` in
        // `RenderStartup`; this test runs the prepare systems in `Update`, so it supplies them.
        world.insert_resource(fallbacks);

        const auto allocate_one = [&](pbr::MaterialBindGroupAllocator& allocator) {
            const auto buffer = device->get().createBuffer(wgpu::BufferDescriptor().setSize(16).setUsage(
                wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst));
            render::render_resource::BindingResources bindings;
            bindings.push_back(0, render::render_resource::BindingResources::buffer(buffer));
            bindings.push_back(1, render::render_resource::BindingResources::data(
                                      render::render_resource::OwnedData{std::vector<std::byte>(16)}));
            return allocator.allocate_unprepared(
                render::render_resource::UnpreparedBindGroup{.bindings = std::move(bindings)}, layout);
        };

        const auto make_allocator = [&](std::string_view label) {
            return pbr::MaterialBindGroupAllocator::make(
                device->get(), label, descriptor, layout,
                render::render_resource::BindlessSlabResourceLimit{.custom = kCapacity});
        };

        auto first  = make_allocator("first prep material");
        auto second = make_allocator("second prep material");
        first_id  = allocate_one(first);
        second_id = allocate_one(second);

        const auto before = first.get(first_id.group);
        ASSERT_TRUE(before.has_value());
        EXPECT_EQ(before->bind_group(), nullptr)
            << "an allocated material has no bind group until the prepare systems build one";

        auto& allocators = world.resource_mut<pbr::MaterialBindGroupAllocators>();
        allocators.insert(epix::meta::type_id<FirstPreparedMaterial>(), std::move(first));
        allocators.insert(epix::meta::type_id<SecondPreparedMaterial>(), std::move(second));
    }

    // Bevy runs the pair in the render world's `PrepareBindGroups`. This test drives the same
    // two systems in `Update` instead of `RenderStartup`, because that schedule is empty here
    // while the render world's startup holds systems this test has no app for.
    render.add_systems(epix::app::Update,
                       ecs::into(pbr::prepare_material_bind_groups).set_name("prepare material bind groups"));
    render.add_systems(epix::app::Update, ecs::into(pbr::write_material_bind_group_buffers)
                                              .after(pbr::prepare_material_bind_groups)
                                              .set_name("write material bind group buffers"));
    render.run_schedule(epix::app::Update);

    {
        auto& world     = render.world_mut();
        auto& allocators = world.resource_mut<pbr::MaterialBindGroupAllocators>();
        const auto* first_allocator  = allocators.get(epix::meta::type_id<FirstPreparedMaterial>());
        const auto* second_allocator = allocators.get(epix::meta::type_id<SecondPreparedMaterial>());
        ASSERT_NE(first_allocator, nullptr);
        ASSERT_NE(second_allocator, nullptr);

        const auto first_slab  = first_allocator->get(first_id.group);
        const auto second_slab = second_allocator->get(second_id.group);
        ASSERT_TRUE(first_slab.has_value());
        ASSERT_TRUE(second_slab.has_value());
        EXPECT_NE(first_slab->bind_group(), nullptr) << "the systems built the first material's group";
        EXPECT_NE(second_slab->bind_group(), nullptr)
            << "and the second's, which a walk that stopped at the first allocator would miss";
    }
}

/** @brief A buffer array's base entry carries its first buffer as well as the chain (the
 *  minimal case behind BND-5).
 *
 *  wgpu-native validates an entry's own resource alongside `BindGroupEntryExtras`, so an
 *  entry carrying only the chain declares an empty array and is rejected with "Array binding
 *  provided zero elements" - while a bind group is still handed back, which is what let the
 *  fault go unnoticed until the bindless slab exposed it. */
TEST(MaterialBindGroupAllocator, BufferArrayEntriesCarryTheirFirstElement) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::shader::ShaderPlugin{});
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    auto& world       = render_app->get().world_mut();
    const auto device = world.get_resource<wgpu::Device>();
    ASSERT_TRUE(device.has_value());

    const auto buffer = device->get().createBuffer(
        wgpu::BufferDescriptor().setSize(16).setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst));

    const auto layout = device->get().createBindGroupLayout(
        wgpu::BindGroupLayoutDescriptor()
            .setLabel("base buffer array layout")
            .setEntries(std::array{render::render_resource::binding_types::storage_buffer_read_only(false, 16)
                                       .count(4)
                                       .build(0, wgpu::ShaderStage::eFragment)}));

    wgpu::BindGroupEntryExtras extras;
    extras.buffers.clear();
    extras.buffers.push_back(buffer);
    const wgpu::BindGroupEntry entry =
        wgpu::BindGroupEntry().setBinding(0).setBuffer(buffer).setNextInChain(extras);

    const auto group = device->get().createBindGroup(wgpu::BindGroupDescriptor()
                                                         .setLabel("base buffer array group")
                                                         .setLayout(layout)
                                                         .setEntries(std::array{entry}));
    EXPECT_TRUE(group);
}

/** @brief The bindless defaults Bevy's `AsBindGroup` derive generates (Bevy
 *  `bevy_render/macros/src/as_bind_group.rs:955-1040`).
 *
 *  A material without the bindless attribute is not bindless: its slot count and descriptor
 *  are nothing, and nothing ever asks whether the device could support an array for it - the
 *  one call site is guarded by the slot count, which is exactly why the derive emits no
 *  support check in that case. For a material that did opt in, support is the derive's
 *  condition on the device: the binding-array features, storage buffers, and enough samplers
 *  for every sampler binding in the array. */
TEST(MaterialBindGroupAllocator, BindlessDefaultsAndTheDerivesSupportCondition) {
    // The defaults need no device.
    EXPECT_FALSE(render::render_resource::AsBindGroupBase::bindless_slot_count().has_value())
        << "a material that never opted in has no bindless slots";
    EXPECT_FALSE(render::render_resource::AsBindGroupBase::bindless_descriptor().has_value());

    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::shader::ShaderPlugin{});
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    const auto device = render_app->get().world().get_resource<wgpu::Device>();
    ASSERT_TRUE(device.has_value());

    // The engine builds its device under the Functionality priority, so the binding-array
    // features are requested: one sampler binding with four slots is supported...
    EXPECT_TRUE(render::render_resource::AsBindGroupBase::bindless_supported(device->get(), 1, 4))
        << "the binding-array features are present and four samplers fit";
    // ...while a slot count no adapter has samplers for is refused, which is the half of the
    // condition that depends on the limit rather than on the features.
    EXPECT_FALSE(render::render_resource::AsBindGroupBase::bindless_supported(device->get(), 1, 100000000))
        << "no adapter has that many samplers, so the array cannot be supported";
}

namespace {
/** @brief A material that never opted into bindless. */
struct NonBindlessMaterial : pbr::Material<NonBindlessMaterial> {};

/** @brief A material that opted in, hiding the default support answer with its own
 *  one-argument one - which is what a real bindless material does, since Bevy's derive bakes
 *  the counts into the generated method. */
struct BindlessMaterial : pbr::Material<BindlessMaterial> {
    [[nodiscard]] static std::optional<render::render_resource::BindlessSlabResourceLimit> bindless_slot_count() {
        return render::render_resource::BindlessSlabResourceLimit{.custom = 4};
    }
    [[nodiscard]] static bool bindless_supported(const wgpu::Device& device) {
        return pbr::Material<BindlessMaterial>::bindless_supported(device, /*sampler_binding_count=*/1,
                                                                  /*bindless_count=*/4);
    }
};

/** @brief A material whose slabs would hold a single slot. */
struct SingleSlotMaterial : pbr::Material<SingleSlotMaterial> {
    [[nodiscard]] static std::optional<render::render_resource::BindlessSlabResourceLimit> bindless_slot_count() {
        return render::render_resource::BindlessSlabResourceLimit{.custom = 1};
    }
    [[nodiscard]] static bool bindless_supported(const wgpu::Device& device) {
        return pbr::Material<SingleSlotMaterial>::bindless_supported(device, 1, 1);
    }
};
}  // namespace

/** @brief Whether a material ends up bindless (Bevy `material_uses_bindless_resources`,
 *  `bevy_pbr/src/material_bind_groups.rs:1681-1687`), on a real device.
 *
 *  Three answers matter and each decides what the material costs to draw. A material that
 *  never opted in is not bindless - and is never asked whether the device could support it,
 *  which is why the derive emits no support check for one. A material with a slab size the
 *  device can serve uses bindless. And a material whose slabs would hold a single slot does
 *  **not**, because Bevy's `> 1` guard refuses to spend a whole bind group on one material.
 */
TEST(MaterialBindGroupAllocator, MaterialsDecideWhetherTheyUseBindlessResources) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::shader::ShaderPlugin{});
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    const auto device = render_app->get().world().get_resource<wgpu::Device>();
    ASSERT_TRUE(device.has_value());

    EXPECT_FALSE(pbr::material_uses_bindless_resources<NonBindlessMaterial>(device->get()))
        << "a material that never opted in is drawn non-bindless";
    EXPECT_TRUE(pbr::material_uses_bindless_resources<BindlessMaterial>(device->get()))
        << "a material with a slab size the device can serve uses bindless";
    EXPECT_FALSE(pbr::material_uses_bindless_resources<SingleSlotMaterial>(device->get()))
        << "one slot is not worth a whole bind group, so Bevy draws it non-bindless";
}

namespace meta = epix::meta;  // this test sits outside `epix`, so the namespace needs naming

namespace {
/** @brief Two material types that register nothing of their own. */
struct FirstRegisteredMaterial {};
struct SecondRegisteredMaterial {};
}  // namespace

/** @brief The allocator registry is keyed by material type (Bevy `MaterialBindGroupAllocators`,
 *  `material_bind_groups.rs:36`, with the `insert` its `Deref` provides, used at
 *  `material.rs:410`).
 *
 *  One registry serves every material type, which is what makes the registration additive: a
 *  second material plugin adds beside the first rather than wiping it, and a type that never
 *  registered answers nothing. */
TEST(MaterialBindGroupAllocator, TheRegistryIsKeyedByMaterialType) {
    pbr::MaterialBindGroupAllocators allocators;
    EXPECT_EQ(allocators.get(meta::type_id<FirstRegisteredMaterial>()), nullptr)
        << "nothing is registered at the start";

    allocators.insert(meta::type_id<FirstRegisteredMaterial>(),
                      pbr::MaterialBindGroupAllocator{pbr::MaterialBindGroupNonBindlessAllocator{"first"}});
    allocators.insert(meta::type_id<SecondRegisteredMaterial>(),
                      pbr::MaterialBindGroupAllocator{pbr::MaterialBindGroupNonBindlessAllocator{"second"}});

    const auto* first = allocators.get(meta::type_id<FirstRegisteredMaterial>());
    ASSERT_NE(first, nullptr);
    EXPECT_TRUE(std::holds_alternative<pbr::MaterialBindGroupNonBindlessAllocator>(first->value));
    EXPECT_NE(allocators.get(meta::type_id<SecondRegisteredMaterial>()), nullptr);
    EXPECT_EQ(allocators.allocators.size(), 2u) << "each type has its own entry";
    EXPECT_EQ(allocators.get(meta::type_id<int>()), nullptr) << "a type that never registered has none";

    // Registering again replaces that type's entry, leaving the other alone - a second
    // plugin for the same material type must not leave two allocators behind.
    allocators.insert(meta::type_id<FirstRegisteredMaterial>(),
                      pbr::MaterialBindGroupAllocator{pbr::MaterialBindGroupNonBindlessAllocator{"first again"}});
    EXPECT_EQ(allocators.allocators.size(), 2u);
    EXPECT_NE(allocators.get(meta::type_id<SecondRegisteredMaterial>()), nullptr) << "the other survives";
}

namespace {
/** @brief A material that specializes `AsBindGroup`, so `MaterialPlugin` has a label and a
 *  layout to register an allocator and a render asset with. */
struct MaterialPrepMaterial : pbr::Material<MaterialPrepMaterial> {};
}  // namespace

/** @brief A specialization supplying exactly what `AsBindGroupImpl` requires, with the
 *  bindless members inherited from `AsBindGroupBase`. The bind-group factory is a stub: this
 *  test exists to *compile* the preparing path, not to run a frame. */
template <>
struct epix::render::render_resource::AsBindGroup<MaterialPrepMaterial>
    : epix::render::render_resource::AsBindGroupBase {
    using Data  = std::monostate;
    using Param = std::tuple<>;

    static constexpr std::string_view label() { return "material_prep_test"; }
    static std::vector<wgpu::BindGroupLayoutEntry> layout_entries() { return {}; }
    static Data bind_group_data(const MaterialPrepMaterial&) { return {}; }
    static std::expected<epix::render::render_resource::UnpreparedBindGroup,
                         epix::render::render_resource::AsBindGroupError>
    unprepared_bind_group(const wgpu::Device&, const wgpu::BindGroupLayout&, const MaterialPrepMaterial&, Param&) {
        return epix::render::render_resource::UnpreparedBindGroup{};
    }
};

/** @brief Registering a material type instantiates the whole preparing path
 *  (`MaterialPlugin<M>` -> `ErasedRenderAssetPlugin<MeshMaterial3d<M>>` ->
 *  `prepare_erased_assets<A>` -> the `ErasedRenderAsset` specialization's `prepare_asset`).
 *
 *  That is the point of the test. A template specialization that compiles inside a header
 *  proves nothing, because nothing instantiates it - the fault PBR-66 was sitting on. Adding
 *  the plugin forces every one of those bodies to be compiled, so a mistake in the preparing
 *  path fails here rather than at the first application that uses a material. */
TEST(MaterialBindGroupAllocator, RegisteringAMaterialInstantiatesThePreparingPath) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::shader::ShaderPlugin{});
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    // What `MaterialPlugin` registers for the prepass phases needs the core graph's
    // `DrawFunctions` and the pipeline the material pipeline is chained after, exactly as a
    // real app gets both from `DefaultPlugins`.
    app.add_plugins(epix::core_graph::CoreGraphPlugin{});

    // The globals a material type shares are `MaterialsPlugin`'s, which `PbrPlugin` installs
    // in a real app (`lib.rs:222-228`); `MaterialPlugin<M>` registers its own type's pieces.
    app.add_plugins(pbr::MaterialsPlugin{});

    // The assertion is that this compiles and runs: the bodies above are instantiated by it.
    app.add_plugins(pbr::MaterialPlugin<MaterialPrepMaterial>{});
    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    EXPECT_TRUE(render_app->get().world().get_resource<pbr::MaterialBindGroupAllocators>().has_value())
        << "MaterialsPlugin initialises the allocator registry every material type shares";
}

/** @brief Which mesh bind group a view gets (Bevy `MeshPhaseBindGroups::get`,
 *  `bevy_pbr/src/render/mesh.rs:2780-2800`).
 *
 *  The three tests are ordered and the order *is* the behaviour, so this pins it: a morphed
 *  mesh takes its own group even when it is also skinned, a skinned mesh takes the shared
 *  skinned group even when it has a lightmap, and only an unskinned, unmorphed mesh
 *  distinguishes its lightmap's group from the shared model-only one. The groups here are
 *  default-constructed, so the assertion is on *which* field came back rather than on a
 *  group's contents - which is exactly the decision being tested. */
TEST(MaterialBindGroupAllocator, MeshPhaseBindGroupsPickByAPriorityOrder) {
    pbr::MeshPhaseBindGroups groups;
    const auto mesh_id       = epix::assets::AssetId<epix::mesh::Mesh>::invalid();
    const auto lightmap_slab = pbr::LightmapSlabIndex{7};

    // Nothing built yet: every answer is nothing, and in particular the shared fields must
    // not be invented.
    EXPECT_EQ(groups.get(mesh_id, std::nullopt, false, false, false), nullptr);
    EXPECT_EQ(groups.get(mesh_id, lightmap_slab, false, false, false), nullptr);
    EXPECT_EQ(groups.get(mesh_id, std::nullopt, true, false, false), nullptr);
    EXPECT_EQ(groups.get(mesh_id, std::nullopt, false, true, false), nullptr);

    groups.model_only.emplace();
    groups.skinned.emplace();
    groups.morph_targets.emplace(mesh_id, pbr::MeshBindGroupPair{});
    groups.lightmaps.emplace(lightmap_slab, wgpu::BindGroup{});

    const wgpu::BindGroup* model_only = &*groups.model_only;
    const wgpu::BindGroup* skinned    = &groups.skinned->no_motion_vectors;
    const wgpu::BindGroup* morphed    = &groups.morph_targets.at(mesh_id).no_motion_vectors;
    const wgpu::BindGroup* lightmapped = &groups.lightmaps.at(lightmap_slab);

    EXPECT_EQ(groups.get(mesh_id, std::nullopt, false, true, false), morphed)
        << "a morphed mesh takes its own group even when it is also skinned";
    EXPECT_EQ(groups.get(mesh_id, lightmap_slab, true, true, false), morphed)
        << "and morphing outranks both skinning and a lightmap";
    EXPECT_EQ(groups.get(mesh_id, lightmap_slab, true, false, false), skinned)
        << "a skinned mesh takes the shared skinned group even when it has a lightmap";
    EXPECT_EQ(groups.get(mesh_id, lightmap_slab, false, false, false), lightmapped)
        << "only an unskinned, unmorphed mesh uses its lightmap's group";
    EXPECT_EQ(groups.get(mesh_id, std::nullopt, false, false, false), model_only)
        << "and with no lightmap it shares the model-only group";

    // The motion-vector flavour is chosen from the view, not the mesh - but only where the
    // groups come in pairs. The model-only group does not vary with it, which is why it is a
    // single group rather than a pair.
    EXPECT_EQ(groups.get(mesh_id, std::nullopt, false, false, true), model_only)
        << "a model-only mesh gets the same group whichever flavour the view wants";
    EXPECT_EQ(groups.get(mesh_id, std::nullopt, false, true, true),
              &groups.morph_targets.at(mesh_id).motion_vectors);

    // A mesh with morph targets that was never built for this id answers nothing rather than
    // falling back to a group that would describe the wrong mesh. (The id used throughout is
    // the invalid one, which is a perfectly good map key.)
    EXPECT_EQ(groups.get(epix::assets::AssetId<epix::mesh::Mesh>::invalid(), std::nullopt, false, true, false),
              morphed)
        << "the invalid id is the one that was built";

    groups.reset();
    EXPECT_FALSE(groups.model_only.has_value());
    EXPECT_FALSE(groups.skinned.has_value());
    EXPECT_TRUE(groups.morph_targets.empty());
    EXPECT_TRUE(groups.lightmaps.empty());
    EXPECT_EQ(groups.get(mesh_id, std::nullopt, false, false, false), nullptr)
        << "and after a reset nothing is answered";
}