#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/image.hpp>
#include <epix/pbr/material.hpp>
#include <epix/pbr/material_bind_groups.hpp>
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
    EXPECT_EQ(pbr::MATERIAL_BIND_GROUP_INDEX, 2u) << "Bevy's MATERIAL_BIND_GROUP";
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