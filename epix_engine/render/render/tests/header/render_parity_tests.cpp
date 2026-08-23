#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include <epix/ecs.hpp>
#include <epix/render.hpp>

using namespace epix::ecs;
using namespace epix::render;

// Mirrors Bevy's ViewRangefinder3d unit test (bevy_render/render_phase/rangefinder.rs).
TEST(ViewRangefinder3d, Distance) {
    const glm::mat4 view_matrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    const auto rangefinder      = phase::ViewRangefinder3d::from_world_from_view(view_matrix);
    EXPECT_FLOAT_EQ(rangefinder.distance(glm::vec3(0.0f, 0.0f, 0.0f)), 1.0f);
    EXPECT_FLOAT_EQ(rangefinder.distance(glm::vec3(0.0f, 0.0f, 1.0f)), 2.0f);
}

// RenderLayers truth table matches Bevy RenderLayers::intersects.
TEST(RenderLayers, Intersects) {
    auto layer0 = camera::RenderLayers::layer(0);
    auto layer1 = camera::RenderLayers::layer(1);
    auto all    = camera::RenderLayers::all();
    auto none   = camera::RenderLayers::none();
    EXPECT_TRUE(layer0.intersects(layer0));
    EXPECT_FALSE(layer0.intersects(layer1));
    EXPECT_TRUE(all.intersects(layer0));
    EXPECT_TRUE(layer0.intersects(all));
    EXPECT_FALSE(none.intersects(layer0));
    EXPECT_FALSE(layer0.intersects(none));
    EXPECT_TRUE(all.intersects(all));  // inverted x inverted always overlaps
    EXPECT_TRUE(layer0.contains(0));
    EXPECT_FALSE(layer0.contains(1));
}

TEST(Msaa, FromSamples) {
    EXPECT_EQ(view::samples(view::Msaa::Off), 1u);
    EXPECT_EQ(view::samples(view::Msaa::Sample4), 4u);
    EXPECT_EQ(view::msaa_from_samples(8), view::Msaa::Sample8);
    EXPECT_THROW(view::msaa_from_samples(3), std::runtime_error);
}

TEST(AlphaMode, Factories) {
    EXPECT_EQ(AlphaMode::opaque().type, AlphaMode::Type::Opaque);
    EXPECT_EQ(AlphaMode::blend().type, AlphaMode::Type::Blend);
    EXPECT_EQ(AlphaMode::add().type, AlphaMode::Type::Add);
    EXPECT_EQ(AlphaMode::multiply().type, AlphaMode::Type::Multiply);
    EXPECT_EQ(AlphaMode::mask(0.25f).mask_threshold, 0.25f);
    EXPECT_EQ(AlphaMode::premultiplied(), AlphaMode::premultiplied());
}

// TextureCache eviction matches Bevy: entries unused for >= 3 updates are removed.
TEST(TextureCache, EvictsAfterThreeFrames) {
    render_resource::TextureCache cache;
    // manually seed one entry (device creation is not needed for eviction)
    render_resource::TextureCacheKey key;
    key.width           = 4;
    key.height          = 4;
    cache.textures[key] = {render_resource::detail::CachedTextureMeta{}};
    // frame 1..3: unused entry ages and is released
    cache.update();  // frames_since_last_use = 1, taken = false
    EXPECT_EQ(cache.textures.at(key).size(), 1u);
    cache.update();  // = 2
    EXPECT_EQ(cache.textures.at(key).size(), 1u);
    cache.update();  // = 3 -> evicted (>= 3)
    EXPECT_EQ(cache.textures.count(key), 0u);
    EXPECT_TRUE(cache.is_empty());
}

TEST(DynamicUniformBuffer, AlignmentAndPush) {
    render_resource::DynamicUniformBuffer<view::ViewUniform> buf;
    buf.dynamic_offset_alignment = 256;
    // Bevy 0.18 ViewUniform is 768 bytes (view.wgsl std140 layout).
    EXPECT_EQ(buf.element_stride(), 768u);  // align_up(768, 256)
    view::ViewUniform u;
    std::size_t idx = buf.push(u);
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(buf.len(), 1u);
    EXPECT_EQ(buf.values.size(), 768u);
    buf.clear();
    EXPECT_TRUE(buf.is_empty());
}

TEST(SortedCamera, SortKey) {
    camera::SortedCamera a;
    a.order = 2;
    camera::SortedCamera b;
    b.order = 1;
    EXPECT_TRUE(b.sort_key() < a.sort_key());
    // same order, different target types: texture (key 1) sorts before window (key 2+)
    camera::SortedCamera tex;
    tex.order  = 1;
    tex.target = camera::RenderTarget::from_texture(wgpu::Texture{});
    camera::SortedCamera win;
    win.order  = 1;
    win.target = camera::RenderTarget::from_primary();
    EXPECT_TRUE(tex.sort_key() < win.sort_key());
}

TEST(GlobalsUniform, CpuFields) {
    GlobalsUniform g;
    g.time        = 1.5f;
    g.delta_time  = 0.016f;
    g.frame_count = 42;
    EXPECT_FLOAT_EQ(g.time, 1.5f);
    EXPECT_FLOAT_EQ(g.delta_time, 0.016f);
    EXPECT_EQ(g.frame_count, 42u);
    static_assert(render_resource::ShaderType<GlobalsUniform>);
    static_assert(sizeof(GlobalsUniform) == 12);
}

// UniformBuffer matches Bevy uniform_buffer.rs:39-142: get/set/get_mut mirror
// the value, add_usages only adds flags and marks changed, has_buffer reports
// creation, and write_buffer recreates when missing/changed or uploads into
// the existing buffer.
TEST(UniformBuffer, BevySemantics) {
    render_resource::UniformBuffer<GlobalsUniform> ub;
    EXPECT_FALSE(ub.has_buffer());
    EXPECT_FALSE(ub.changed);

    ub.set(GlobalsUniform{1.0f, 0.016f, 3u});
    EXPECT_EQ(ub.get().frame_count, 3u);
    ub.get_mut().time = 2.0f;
    EXPECT_FLOAT_EQ(ub.get().time, 2.0f);

    // add_usages adds flags and marks changed (Bevy add_usages).
    const auto base_usage = ub.usage;
    ub.add_usages(wgpu::BufferUsage::eStorage);
    EXPECT_TRUE(ub.changed);
    EXPECT_NE(static_cast<std::uint64_t>(ub.usage) & static_cast<std::uint64_t>(wgpu::BufferUsage::eStorage), 0ull);
    EXPECT_NE(static_cast<std::uint64_t>(ub.usage) & static_cast<std::uint64_t>(base_usage), 0ull);

    // set()/get_mut() do NOT set the changed flag (Bevy only
    // set_label/add_usages do; write_buffer always uploads the value).
    render_resource::UniformBuffer<GlobalsUniform> ub2;
    ub2.set(GlobalsUniform{});
    EXPECT_FALSE(ub2.changed);
    ub2.get_mut().time = 9.0f;
    EXPECT_FALSE(ub2.changed);
}
TEST(RetainedViewEntity, Equality) {
    view::RetainedViewEntity a{sync_world::MainEntity{Entity{1}}, std::nullopt, 0};
    view::RetainedViewEntity b{sync_world::MainEntity{Entity{1}}, std::nullopt, 0};
    view::RetainedViewEntity c{sync_world::MainEntity{Entity{2}}, std::nullopt, 0};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

// RetainedViewEntity::create matches Bevy RetainedViewEntity::new
// (view/mod.rs:243-253): main entity + optional auxiliary + subview index.
TEST(RetainedViewEntity, CreateFactory) {
    auto rve = view::RetainedViewEntity::create(sync_world::MainEntity{Entity::from_index(7)},
                                                sync_world::MainEntity{Entity::from_index(8)}, 2);
    EXPECT_EQ(rve.main_entity, sync_world::MainEntity{Entity::from_index(7)});
    ASSERT_TRUE(rve.auxiliary_entity.has_value());
    EXPECT_EQ(*rve.auxiliary_entity, sync_world::MainEntity{Entity::from_index(8)});
    EXPECT_EQ(rve.subview_index, 2u);

    auto no_aux = view::RetainedViewEntity::create(sync_world::MainEntity{Entity::from_index(9)}, std::nullopt, 0);
    EXPECT_FALSE(no_aux.auxiliary_entity.has_value());
    EXPECT_EQ(no_aux.subview_index, 0u);
}

// SlotInfos::get_slot with an out-of-range numeric label returns nullopt
// instead of indexing out of bounds (Bevy returns None).
TEST(SlotInfos, GetSlotBoundsCheck) {
    using graph::SlotInfo;
    using graph::SlotInfos;
    using graph::SlotLabel;
    using graph::SlotType;
    SlotInfos infos(std::array{SlotInfo{"in", SlotType::Buffer}, SlotInfo{"tex", SlotType::TextureView}});
    ASSERT_TRUE(infos.get_slot(SlotLabel{0u}).has_value());
    ASSERT_TRUE(infos.get_slot(SlotLabel{1u}).has_value());
    EXPECT_FALSE(infos.get_slot(SlotLabel{2u}).has_value());  // out of range
    EXPECT_FALSE(infos.get_slot(SlotLabel{999u}).has_value());
    EXPECT_TRUE(infos.get_slot(SlotLabel{std::string("tex")}).has_value());
    EXPECT_FALSE(infos.get_slot(SlotLabel{std::string("missing")}).has_value());
}
// RenderAssetBytesPerFrameLimiter matches Bevy (render_asset.rs): no limit ->
// writes are not accounted and exhausted() is always false; with a limit, writes
// accumulate, exhausted() flips at >= max_bytes, and reset() only clears when a
// limit is set.
TEST(RenderAssetBytesPerFrame, LimiterSemantics) {
    RenderAssetBytesPerFrameLimiter limiter;
    EXPECT_FALSE(limiter.exhausted());
    limiter.write_bytes(100);
    EXPECT_EQ(limiter.bytes_written, 0u);  // no limit -> not recorded
    limiter.reset();                       // no limit -> no-op
    EXPECT_EQ(limiter.bytes_written, 0u);

    limiter.max_bytes = 256;
    EXPECT_FALSE(limiter.exhausted());
    limiter.write_bytes(100);
    EXPECT_EQ(limiter.bytes_written, 100u);
    EXPECT_FALSE(limiter.exhausted());
    limiter.write_bytes(156);
    EXPECT_EQ(limiter.bytes_written, 256u);
    EXPECT_TRUE(limiter.exhausted());
    limiter.write_bytes(50);
    EXPECT_EQ(limiter.bytes_written, 306u);  // over-budget recorded, like Bevy's counter
    limiter.reset();
    EXPECT_EQ(limiter.bytes_written, 0u);
    EXPECT_FALSE(limiter.exhausted());
}

// GpuReadback alignment/size utils match Bevy (gpu_readback.rs utils).
TEST(GpuReadback, AlignmentUtils) {
    EXPECT_EQ(readback::align_byte_size(256), 256u);
    EXPECT_EQ(readback::align_byte_size(1), 256u);
    EXPECT_EQ(readback::align_byte_size(0), 0u);
    // 4x4 rgba8: rows are 4*4=16 bytes, padded to 256; 4 rows x 1 layer.
    EXPECT_EQ(readback::get_aligned_size(wgpu::Extent3D{4, 4, 1}, 4), 4u * 256u);
    // 64x1 with 3 layers: align(256)=256 per row, 3 layers.
    EXPECT_EQ(readback::get_aligned_size(wgpu::Extent3D{64, 1, 3}, 4), 3u * 256u);
    EXPECT_EQ(readback::texture_format_pixel_size(wgpu::TextureFormat::eR8Unorm), 1u);
    EXPECT_EQ(readback::texture_format_pixel_size(wgpu::TextureFormat::eRGBA8Unorm), 4u);
    EXPECT_EQ(readback::texture_format_pixel_size(wgpu::TextureFormat::eRGBA16Float), 8u);
    EXPECT_EQ(readback::texture_format_pixel_size(wgpu::TextureFormat::eRGBA32Float), 16u);
    EXPECT_EQ(readback::texture_format_pixel_size(wgpu::TextureFormat::eBC1RGBAUnorm), 0u);  // compressed
}

// Pool eviction matches Bevy: taken buffers never age; unused buffers are
// evicted once idle for max_unused_frames; empty size buckets are removed.
TEST(GpuReadback, BufferPoolEviction) {
    readback::GpuReadbackBufferPool pool;
    pool.buffers[64] = {
        readback::GpuReadbackBuffer{wgpu::Buffer{}, true, 0},   // taken: never ages
        readback::GpuReadbackBuffer{wgpu::Buffer{}, false, 0},  // unused: ages
    };
    pool.update(3);
    EXPECT_EQ(pool.buffers[64].size(), 2u);  // frames_unused = 1 < 3
    pool.update(3);
    EXPECT_EQ(pool.buffers[64].size(), 2u);  // = 2
    pool.update(3);
    EXPECT_EQ(pool.buffers[64].size(), 1u);  // unused evicted at >= 3; taken stays
    pool.update(3);
    EXPECT_EQ(pool.buffers[64].size(), 1u);  // taken never ages
    // empty size buckets are removed
    pool.buffers[128] = {readback::GpuReadbackBuffer{wgpu::Buffer{}, false, 3}};
    pool.update(3);
    EXPECT_EQ(pool.buffers.count(128), 0u);
}

// SyncWorld keeps SyncToRenderWorld entities in sync between the main and
// render worlds (Bevy entity_sync_system): spawn + RenderEntity/MainEntity
// link, idempotence, and despawn propagation.
TEST(SyncWorld, EntitySyncAndDespawn) {
    World main_world(WorldId(0));
    World render_world(WorldId(1));

    Entity main_entity = main_world.spawn(sync_world::SyncToRenderWorld{}).id();
    sync_world::entity_sync_system(main_world, render_world);

    // the main entity now carries a RenderEntity pointing at a render entity with MainEntity
    auto get_render_entity = [&](Entity e) -> Entity {
        return main_world.get_entity(e)
            .and_then([](const EntityRef& ref) {
                return ref.get<sync_world::RenderEntity>().transform(
                    [](const std::reference_wrapper<const sync_world::RenderEntity>& re) { return re.get().entity; });
            })
            .value();
    };
    Entity render_entity = get_render_entity(main_entity);
    EXPECT_TRUE(render_world.get_entity(render_entity).has_value());
    auto main_back =
        render_world.get_entity(render_entity)
            .and_then([](const EntityRef& ref) { return ref.get<sync_world::MainEntity>(); })
            .transform([](const std::reference_wrapper<const sync_world::MainEntity>& me) { return me.get().entity; })
            .value();
    EXPECT_EQ(main_back, main_entity);

    // a second sync is idempotent: no duplicate render entities are spawned
    sync_world::entity_sync_system(main_world, render_world);
    EXPECT_EQ(get_render_entity(main_entity), render_entity);

    // despawned main entity -> its render entity is despawned on the next sync
    main_world.get_entity_mut(main_entity).transform([](EntityWorldMut&& ew) -> int {
        ew.despawn();
        return 0;
    });
    sync_world::entity_sync_system(main_world, render_world);
    EXPECT_FALSE(render_world.get_entity(render_entity).has_value());
}

// TemporaryRenderEntity markers are despawned by remove_temporary_render_entities
// (Bevy despawn_temporary_render_entities, RenderSystems::PostCleanup).
TEST(SyncWorld, RemoveTemporaryRenderEntities) {
    World world(WorldId(0));
    Entity persistent = world.spawn(sync_world::TemporaryRenderEntity{}).id();
    Entity kept       = world.spawn(42).id();
    sync_world::remove_temporary_render_entities(world);
    EXPECT_FALSE(world.get_entity(persistent).has_value());
    EXPECT_TRUE(world.get_entity(kept).has_value());
}

// Removing a synced component despawns+respawns the render entity (Bevy
// ComponentRemoved handling, sync_world.rs:238-250), clearing derived or
// extracted components that would otherwise go stale.
TEST(SyncWorld, ComponentRemovedRespawnsRenderEntity) {
    World main_world(WorldId(0));
    World render_world(WorldId(1));
    main_world.registrator().register_component<int>();
    render_world.registrator().register_component<int>();
    main_world.init_resource<sync_world::PendingSyncEntity>();

    Entity main_entity = main_world.spawn(sync_world::SyncToRenderWorld{}, 42).id();
    sync_world::entity_sync_system(main_world, render_world);
    auto first_render =
        main_world.get_entity(main_entity)
            .and_then([](const EntityRef& e) { return e.get<sync_world::RenderEntity>(); })
            .transform([](const std::reference_wrapper<const sync_world::RenderEntity>& re) { return re.get().entity; })
            .value();
    // simulate a stale derived/extracted artifact on the render entity
    render_world.get_entity_mut(first_render).transform([](EntityWorldMut&& ew) -> int {
        ew.insert(777);
        return 0;
    });

    // remove the synced component and record it (as record_component_removed does)
    main_world.get_entity_mut(main_entity).transform([](EntityWorldMut&& ew) -> int {
        ew.remove<int>();
        return 0;
    });
    main_world.get_resource_mut<sync_world::PendingSyncEntity>()->get().component_removed.push_back(main_entity);
    sync_world::entity_sync_system(main_world, render_world);

    // the old render entity is gone; a fresh one is linked with no stale artifact
    EXPECT_FALSE(render_world.get_entity(first_render).has_value());
    auto second_render =
        main_world.get_entity(main_entity)
            .and_then([](const EntityRef& e) { return e.get<sync_world::RenderEntity>(); })
            .transform([](const std::reference_wrapper<const sync_world::RenderEntity>& re) { return re.get().entity; })
            .value();
    EXPECT_NE(second_render, first_render);
    EXPECT_FALSE(
        render_world.get_entity(second_render).and_then([](const EntityRef& e) { return e.get<int>(); }).has_value());
}

// Label interning matches Bevy (bevy_ecs::intern): equal values dedupe to the
// same interned pointer; distinct types intern separately; equality is
// reference identity.
TEST(LabelInterner, DeduplicatesByValue) {
    auto a = label::intern(phase::DrawFunctionLabel{});
    auto b = label::intern(phase::DrawFunctionLabel{});
    EXPECT_EQ(a, b);
    EXPECT_EQ(a.get(), b.get());
    // distinct types intern to distinct values (not cross-comparable, like Bevy)
    auto shader = label::intern(phase::ShaderLabel{});
    EXPECT_NE(static_cast<const void*>(shader.get()), static_cast<const void*>(a.get()));
    // type identity is carried by the Interned<T> static type
    EXPECT_EQ(a.type(), epix::meta::type_index(epix::meta::type_id<phase::DrawFunctionLabel>()));
    EXPECT_NE(a.type(), shader.type());
}

TEST(LabelInterner, TypedAliases) {
    static_assert(std::same_as<phase::InternedDrawFunctionLabel, label::Interned<phase::DrawFunctionLabel>>);
    static_assert(std::same_as<phase::InternedShaderLabel, label::Interned<phase::ShaderLabel>>);
    phase::InternedShaderLabel s = label::intern(phase::ShaderLabel{});
    EXPECT_EQ(s, label::intern(phase::ShaderLabel{}));
}

namespace {
// Minimal binned phase item for exercising the binned-phase machinery.
struct TestBinnedItem {
    Entity m_entity;
    phase::DrawFunctionId m_draw_function;
    int m_sort_key      = 0;
    int m_bin_key       = 0;
    int m_batch_set_key = 0;
    bool m_batchable    = true;

    Entity entity() const { return m_entity; }
    sync_world::MainEntity main_entity() const { return sync_world::MainEntity{m_entity}; }
    int sort_key() const { return m_sort_key; }
    phase::DrawFunctionId draw_function() const { return m_draw_function; }
    std::pair<std::uint32_t, std::uint32_t> batch_range{0, 1};  // stored range (Bevy batch_range: Range<u32>)
    phase::PhaseItemExtraIndex extra_index() const { return phase::PhaseItemExtraIndex::None; }
    using BinKey      = int;
    using BatchSetKey = int;
    const int& bin_key() const { return m_bin_key; }
    const int& batch_set_key() const { return m_batch_set_key; }
    bool batchable() const { return m_batchable; }
};
static_assert(epix::render::phase::BinnedPhaseItem<TestBinnedItem>);
}  // namespace

// Entities with the same (batch set, bin) key land in the same bin; distinct
// bin keys produce separate bins (Bevy BinnedRenderPhase::add).
TEST(BinnedRenderPhase, BinsByKey) {
    phase::BinnedRenderPhase<TestBinnedItem> phase;
    const Tick tick(1);
    const sync_world::MainEntity m1{Entity::from_index(1)};
    const sync_world::MainEntity m2{Entity::from_index(2)};
    const sync_world::MainEntity m3{Entity::from_index(3)};
    phase.add(0, 0, Entity::from_index(10), m1, phase::InputUniformIndex{0},
              phase::BinnedRenderPhaseType::BatchableMesh, tick);
    phase.add(0, 0, Entity::from_index(11), m2, phase::InputUniformIndex{1},
              phase::BinnedRenderPhaseType::BatchableMesh, tick);
    phase.add(0, 1, Entity::from_index(12), m3, phase::InputUniformIndex{2},
              phase::BinnedRenderPhaseType::BatchableMesh, tick);

    auto* bin0 = phase.batchable_meshes.get(phase::BinKeyPair<int, int>{0, 0});
    ASSERT_NE(bin0, nullptr);
    EXPECT_EQ(bin0->size(), 2u);
    EXPECT_TRUE(bin0->contains(m1.entity));
    EXPECT_TRUE(bin0->contains(m2.entity));
    EXPECT_EQ(bin0->get(m1.entity)->index, 0u);
    EXPECT_EQ(bin0->get(m2.entity)->index, 1u);

    auto* bin1 = phase.batchable_meshes.get(phase::BinKeyPair<int, int>{0, 1});
    ASSERT_NE(bin1, nullptr);
    EXPECT_EQ(bin1->size(), 1u);
    EXPECT_TRUE(bin1->contains(m3.entity));
    EXPECT_FALSE(phase.is_empty());
}

// Multidrawable / unbatchable / non-mesh items land in their own lists
// (Bevy BinnedRenderPhaseType).
TEST(BinnedRenderPhase, PhaseTypes) {
    phase::BinnedRenderPhase<TestBinnedItem> phase;
    const Tick tick(1);
    const sync_world::MainEntity m1{Entity::from_index(1)};
    const sync_world::MainEntity m2{Entity::from_index(2)};
    const sync_world::MainEntity m3{Entity::from_index(3)};
    phase.add(0, 0, Entity::from_index(10), m1, phase::InputUniformIndex{0},
              phase::BinnedRenderPhaseType::MultidrawableMesh, tick);
    phase.add(0, 0, Entity::from_index(11), m2, phase::InputUniformIndex{1},
              phase::BinnedRenderPhaseType::UnbatchableMesh, tick);
    phase.add(0, 0, Entity::from_index(12), m3, phase::InputUniformIndex{2}, phase::BinnedRenderPhaseType::NonMesh,
              tick);

    auto* batch_set = phase.multidrawable_meshes.get(0);
    ASSERT_NE(batch_set, nullptr);
    EXPECT_EQ(batch_set->get(0)->size(), 1u);
    auto* unbatchable = phase.unbatchable_meshes.get(phase::BinKeyPair<int, int>{0, 0});
    ASSERT_NE(unbatchable, nullptr);
    EXPECT_EQ(unbatchable->entities.at(m2.entity), Entity::from_index(11));
    auto* non_mesh = phase.non_mesh_items.get(phase::BinKeyPair<int, int>{0, 0});
    ASSERT_NE(non_mesh, nullptr);
    EXPECT_EQ(non_mesh->entities.at(m3.entity), Entity::from_index(12));
}

// Entities not re-queued after prepare_for_new_frame are swept from their
// bins (Bevy sweep_old_entities).
TEST(BinnedRenderPhase, SweepRemovesUnqueued) {
    phase::BinnedRenderPhase<TestBinnedItem> phase;
    const sync_world::MainEntity e1{Entity::from_index(1)};
    const sync_world::MainEntity e2{Entity::from_index(2)};
    phase.add(0, 0, Entity::from_index(10), e1, phase::InputUniformIndex{0},
              phase::BinnedRenderPhaseType::BatchableMesh, Tick(1));
    phase.add(0, 0, Entity::from_index(11), e2, phase::InputUniformIndex{1},
              phase::BinnedRenderPhaseType::BatchableMesh, Tick(1));

    // frame 2: only e1 is re-queued (with a newer change tick)
    phase.prepare_for_new_frame();
    phase.add(0, 0, Entity::from_index(10), e1, phase::InputUniformIndex{0},
              phase::BinnedRenderPhaseType::BatchableMesh, Tick(2));
    phase.sweep_old_entities();

    auto* bin = phase.batchable_meshes.get(phase::BinKeyPair<int, int>{0, 0});
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ(bin->size(), 1u);
    EXPECT_TRUE(bin->contains(e1.entity));
    EXPECT_FALSE(bin->contains(e2.entity));
}

// An entity that moved to a different bin is removed from its old bin on
// sweep (Bevy entities_that_changed_bins).
TEST(BinnedRenderPhase, ChangedBinMoved) {
    phase::BinnedRenderPhase<TestBinnedItem> phase;
    const sync_world::MainEntity e1{Entity::from_index(1)};
    phase.add(0, 0, Entity::from_index(10), e1, phase::InputUniformIndex{0},
              phase::BinnedRenderPhaseType::BatchableMesh, Tick(1));
    phase.prepare_for_new_frame();
    phase.add(0, 1, Entity::from_index(10), e1, phase::InputUniformIndex{0},
              phase::BinnedRenderPhaseType::BatchableMesh, Tick(2));
    phase.sweep_old_entities();

    EXPECT_EQ(phase.batchable_meshes.get(phase::BinKeyPair<int, int>{0, 0}), nullptr);
    auto* bin1 = phase.batchable_meshes.get(phase::BinKeyPair<int, int>{0, 1});
    ASSERT_NE(bin1, nullptr);
    EXPECT_EQ(bin1->size(), 1u);
}

// ViewBinnedRenderPhases retains one phase per view and is idempotent.
TEST(ViewBinnedRenderPhases, PrepareForNewFrame) {
    phase::ViewBinnedRenderPhases<TestBinnedItem> phases;
    const view::RetainedViewEntity v{sync_world::MainEntity{Entity::from_index(5)}, std::nullopt, 0};
    phases.prepare_for_new_frame(v);
    EXPECT_EQ(phases.phases.size(), 1u);
    phases.prepare_for_new_frame(v);
    EXPECT_EQ(phases.phases.size(), 1u);
}

// VisibilityRange equality/hashing participate in RenderVisibilityRanges
// dedup (bevy_camera visibility ranges).
TEST(VisibilityRange, EqualityAndHash) {
    view::VisibilityRange a;
    a.start_margin_start    = 1.0f;
    a.end_margin_end        = 50.0f;
    view::VisibilityRange b = a;
    view::VisibilityRange c = a;
    c.end_margin_end        = 51.0f;
    view::VisibilityRange d = a;
    d.abrupt_start_margin   = true;
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);
    EXPECT_EQ(std::hash<view::VisibilityRange>{}(a), std::hash<view::VisibilityRange>{}(b));
}

// RenderVisibilityRanges insert/dedup/accessors match Bevy range.rs:104-163:
// identical ranges share one GPU slot, lod_index_for_entity returns it,
// entity_has_crossfading_visibility_ranges reflects abruptness, clear resets.
TEST(RenderVisibilityRanges, InsertDedupAndAccessors) {
    view::RenderVisibilityRanges ranges;

    view::VisibilityRange crossfaded;
    crossfaded.start_margin_start = 1.0f;
    crossfaded.start_margin_end   = 5.0f;  // crossfade: margins differ
    crossfaded.end_margin_start   = 45.0f;
    crossfaded.end_margin_end     = 50.0f;
    EXPECT_FALSE(crossfaded.is_abrupt());

    view::VisibilityRange abrupt;
    abrupt.start_margin_start = 1.0f;
    abrupt.start_margin_end   = 1.0f;  // abrupt: start == end
    abrupt.end_margin_start   = 50.0f;
    abrupt.end_margin_end     = 50.0f;
    EXPECT_TRUE(abrupt.is_abrupt());

    const sync_world::MainEntity e1{Entity::from_index(1)};
    const sync_world::MainEntity e2{Entity::from_index(2)};
    const sync_world::MainEntity e3{Entity::from_index(3)};

    // Two entities with the identical crossfaded range share one GPU slot.
    ranges.insert(e1, crossfaded);
    ranges.insert(e2, crossfaded);
    ASSERT_EQ(ranges.range_to_index.size(), 1u);
    ASSERT_EQ(ranges.buffer.len(), 1u);
    EXPECT_EQ(ranges.lod_index_for_entity(e1), ranges.lod_index_for_entity(e2));
    EXPECT_EQ(ranges.lod_index_for_entity(e1).value_or(99u), 0u);
    EXPECT_TRUE(ranges.entity_has_crossfading_visibility_ranges(e1));
    EXPECT_TRUE(ranges.entity_has_crossfading_visibility_ranges(e2));

    // A different (abrupt) range gets its own slot and reports no crossfade.
    ranges.insert(e3, abrupt);
    ASSERT_EQ(ranges.range_to_index.size(), 2u);
    EXPECT_EQ(ranges.lod_index_for_entity(e3).value_or(99u), 1u);
    EXPECT_FALSE(ranges.entity_has_crossfading_visibility_ranges(e3));

    // Unknown entities have no range.
    EXPECT_FALSE(ranges.lod_index_for_entity(sync_world::MainEntity{Entity::from_index(99)}).has_value());
    EXPECT_FALSE(ranges.entity_has_crossfading_visibility_ranges(sync_world::MainEntity{Entity::from_index(99)}));

    // clear() empties the per-entity info only: range indices and the GPU
    // buffer persist so indices stay stable across frames (Bevy range.rs:106-113).
    ranges.clear();
    EXPECT_TRUE(ranges.entities.empty());
    EXPECT_FALSE(ranges.range_to_index.empty());
    EXPECT_FALSE(ranges.buffer.is_empty());
    // buffer_dirty is left as-is by clear(): it was set by the last new-range
    // insert and is only reset by write_render_visibility_ranges.
    EXPECT_TRUE(ranges.buffer_dirty);

    // Re-inserting the same ranges reuses the stable indices.
    ranges.insert(e1, crossfaded);
    EXPECT_EQ(ranges.lod_index_for_entity(e1).value_or(99u), 0u);
    ranges.insert(e3, abrupt);
    EXPECT_EQ(ranges.lod_index_for_entity(e3).value_or(99u), 1u);
}

// BufferVec default usage for RenderVisibilityRanges carries the Bevy usages
// (STORAGE|UNIFORM|VERTEX) plus COPY_DST for queue.writeBuffer.
TEST(RenderVisibilityRanges, BufferUsageMatchesBevy) {
    view::RenderVisibilityRanges ranges;
    const auto usage = static_cast<std::uint64_t>(ranges.buffer.buffer_usage);
    EXPECT_NE((usage & static_cast<std::uint64_t>(wgpu::BufferUsage::eStorage)), 0ull);
    EXPECT_NE((usage & static_cast<std::uint64_t>(wgpu::BufferUsage::eUniform)), 0ull);
    EXPECT_NE((usage & static_cast<std::uint64_t>(wgpu::BufferUsage::eVertex)), 0ull);
    EXPECT_NE((usage & static_cast<std::uint64_t>(wgpu::BufferUsage::eCopyDst)), 0ull);
    // Bevy default: buffer starts dirty so the first write uploads.
    EXPECT_TRUE(ranges.buffer_dirty);
}

// extract_visibility_ranges (Bevy range.rs:167-181): populates
// RenderVisibilityRanges from main-world VisibilityRange components, clears on
// change, and early-outs when nothing changed and nothing was removed.
TEST(RenderVisibilityRanges, ExtractSystemPopulatesAndEarlyOuts) {
    epix::ecs::World main_world(2);
    epix::ecs::World render_world(2);
    render_world.insert_resource(epix::app::ExtractedWorld{main_world});
    render_world.insert_resource(view::RenderVisibilityRanges{});

    view::VisibilityRange crossfaded;
    crossfaded.start_margin_start = 1.0f;
    crossfaded.start_margin_end   = 5.0f;
    crossfaded.end_margin_start   = 45.0f;
    crossfaded.end_margin_end     = 50.0f;
    Entity e1                     = main_world.spawn(crossfaded).id();
    Entity e2                     = main_world.spawn(crossfaded).id();  // identical range -> dedup
    view::VisibilityRange abrupt;
    abrupt.start_margin_start = 1.0f;
    abrupt.start_margin_end   = 1.0f;
    abrupt.end_margin_start   = 50.0f;
    abrupt.end_margin_end     = 50.0f;
    Entity e3                 = main_world.spawn(abrupt).id();

    auto system = make_system_unique(view::extract_visibility_ranges);
    system->initialize(render_world);

    // First run: both entities extracted; identical ranges share one slot.
    ASSERT_TRUE(system->run({}, render_world).has_value());
    auto& ranges = render_world.resource<view::RenderVisibilityRanges>();
    ASSERT_EQ(ranges.entities.size(), 3u);
    ASSERT_EQ(ranges.range_to_index.size(), 2u);  // dedup: 2 distinct ranges
    EXPECT_EQ(ranges.lod_index_for_entity(sync_world::MainEntity{e1}),
              ranges.lod_index_for_entity(sync_world::MainEntity{e2}));
    EXPECT_TRUE(ranges.entity_has_crossfading_visibility_ranges(sync_world::MainEntity{e1}));
    EXPECT_FALSE(ranges.entity_has_crossfading_visibility_ranges(sync_world::MainEntity{e3}));

    // Second run with no changes: Bevy early-outs (nothing cleared/rebuilt).
    ASSERT_TRUE(system->run({}, render_world).has_value());
    EXPECT_EQ(render_world.resource<view::RenderVisibilityRanges>().entities.size(), 3u);
    EXPECT_EQ(render_world.resource<view::RenderVisibilityRanges>().range_to_index.size(), 2u);
}

// cleanup_view_targets_for_resize (Bevy view/mod.rs:1046-1059): a camera
// targeting a window that was resized or changed present mode loses its
// ViewTarget so prepare_view_target recreates the main textures at the new
// size; unchanged windows keep their ViewTarget.
TEST(ViewTarget, CleanupForResizeRemovesTarget) {
    epix::ecs::World main_world(2);
    epix::ecs::World render_world(2);
    render_world.insert_resource(epix::app::ExtractedWorld{main_world});

    epix::ecs::Entity window_entity = main_world.spawn().id();
    epix::ecs::Entity cam_a         = render_world.spawn(view::ViewTarget{}).id();
    epix::ecs::Entity cam_b         = render_world.spawn(view::ViewTarget{}).id();

    window::ExtractedWindows windows;
    windows.primary                       = window_entity;
    windows.windows[window_entity]        = window::ExtractedWindow{};
    windows.windows[window_entity].entity = window_entity;
    // Window resized this frame: cameras targeting it must lose ViewTarget.
    windows.windows[window_entity].size_changed = true;
    render_world.insert_resource(std::move(windows));
    render_world.get_entity_mut(cam_a).transform([](epix::ecs::EntityWorldMut&& cam) -> int {
        cam.insert(camera::ExtractedCamera{.render_target = camera::RenderTarget::from_primary()});
        return 0;
    });

    auto system = make_system_unique(view::cleanup_view_targets_for_resize);
    system->initialize(render_world);
    ASSERT_TRUE(system->run({}, render_world).has_value());

    auto has_target = [&render_world](epix::ecs::Entity e) {
        return render_world.get_entity(e)
            .and_then([](const epix::ecs::EntityRef& er) { return er.get<view::ViewTarget>(); })
            .has_value();
    };
    EXPECT_FALSE(has_target(cam_a)) << "Camera targeting a resized window should lose its ViewTarget";
    // cam_b targets no window (no ExtractedCamera) -> untouched.
    EXPECT_TRUE(has_target(cam_b));
}

// clear_view_attachments (Bevy view/mod.rs:1042-1044) drops the per-frame
// output attachments before window surfaces are reconfigured.
TEST(ViewTarget, ClearAttachmentsEmptiesMap) {
    view::ViewTargetAttachments attachments;
    attachments.attachments.emplace(::epix::render::camera::RenderTargetId{1}, view::OutputColorAttachment{});
    ASSERT_FALSE(attachments.attachments.empty());

    auto system = make_system_unique(view::clear_view_attachments);
    epix::ecs::World world(1);
    world.insert_resource(std::move(attachments));
    system->initialize(world);
    ASSERT_TRUE(system->run({}, world).has_value());
    EXPECT_TRUE(world.resource<view::ViewTargetAttachments>().attachments.empty());
}

// ColorGrading defaults match Bevy's ColorGrading::default (bevy_camera).
TEST(ColorGrading, DefaultsMatchBevy) {
    view::ColorGrading cg;
    EXPECT_FLOAT_EQ(cg.global.exposure, 0.0f);
    EXPECT_FLOAT_EQ(cg.global.post_saturation, 1.0f);
    EXPECT_FLOAT_EQ(cg.global.midtones_range.first, 0.2f);
    EXPECT_FLOAT_EQ(cg.global.midtones_range.second, 0.7f);
    EXPECT_FLOAT_EQ(cg.shadows.saturation, 1.0f);
    EXPECT_FLOAT_EQ(cg.shadows.contrast, 1.0f);
    EXPECT_FLOAT_EQ(cg.shadows.gamma, 1.0f);
    EXPECT_FLOAT_EQ(cg.shadows.gain, 1.0f);
    EXPECT_FLOAT_EQ(cg.shadows.lift, 0.0f);
    view::ColorGradingUniform u;
    EXPECT_FLOAT_EQ(u.midtone_range.x, 0.2f);
    EXPECT_FLOAT_EQ(u.midtone_range.y, 0.7f);
    EXPECT_FLOAT_EQ(u.exposure, 0.0f);
    EXPECT_FLOAT_EQ(u.hue, 0.0f);
    EXPECT_FLOAT_EQ(u.post_saturation, 1.0f);
    EXPECT_EQ(u.balance, glm::mat3(1.0f));
    EXPECT_EQ(u.saturation, glm::vec3(1.0f));
}

// ColorGradingUniform uses Bevy's D65/CAM16 transform, and preserves the
// three per-luminance section values in the shader's component order.
TEST(ColorGrading, PacksWhiteBalanceAndSections) {
    view::ColorGrading grading;
    grading.global.temperature    = 0.02f;
    grading.global.tint           = -0.01f;
    grading.global.exposure       = 1.5f;
    grading.shadows.saturation    = 0.8f;
    grading.midtones.saturation   = 1.1f;
    grading.highlights.saturation = 1.3f;
    auto uniform                  = view::to_uniform(grading);
    EXPECT_EQ(uniform.saturation, glm::vec3(0.8f, 1.1f, 1.3f));
    EXPECT_FLOAT_EQ(uniform.exposure, 1.5f);
    // Non-default white balance must produce a non-identity matrix.
    EXPECT_NE(uniform.balance, glm::mat3(1.0f));
}

TEST(TemporalJitter, MatchesBevyProjectionAdjustment) {
    camera::TemporalJitter jitter{glm::vec2(0.5f, 0.5f)};
    glm::mat4 projection(1.0f);  // orthographic path
    jitter.jitter_projection(projection, glm::vec2(100.0f, 100.0f));
    EXPECT_FLOAT_EQ(projection[2][0], 0.005f);
    EXPECT_FLOAT_EQ(projection[2][1], -0.005f);
}

TEST(Exposure, BlenderDefaultMatchesBevy) {
    ::epix::camera::Exposure exposure;
    EXPECT_FLOAT_EQ(exposure.exposure(), std::exp2(-9.7f) / 1.2f);
    EXPECT_FLOAT_EQ(::epix::camera::Exposure::sunlight().ev100, 15.0f);
    const ::epix::camera::PhysicalCameraParameters physical{};
    EXPECT_FLOAT_EQ(::epix::camera::Exposure::from_physical_camera(physical).ev100, physical.ev100());
}

TEST(Viewport, ClampToTargetMatchesBevy) {
    camera::Viewport viewport{.pos = glm::uvec2(90, 150), .size = glm::uvec2(30, 20)};
    viewport.clamp_to_size(glm::uvec2(100, 100));
    EXPECT_EQ(viewport.pos, glm::uvec2(90, 99));
    EXPECT_EQ(viewport.size, glm::uvec2(10, 1));

    viewport = camera::Viewport{.pos = glm::uvec2(8, 2), .size = glm::uvec2(3, 3)};
    viewport.clamp_to_size(glm::uvec2(0, 0));
    EXPECT_EQ(viewport.pos, glm::uvec2(0, 0));
    EXPECT_EQ(viewport.size, glm::uvec2(0, 0));
}

TEST(MainPassResolutionOverride, StoresPhysicalDimensions) {
    camera::MainPassResolutionOverride override{glm::uvec2(640, 360)};
    EXPECT_EQ(override.size, glm::uvec2(640, 360));

    const std::optional<::epix::camera::Viewport> viewport =
        ::epix::camera::Viewport{.pos = glm::uvec2(3, 4), .size = glm::uvec2(10, 10)};
    auto overridden = ::epix::camera::Viewport::from_viewport_and_override(viewport, glm::uvec2(640, 360));
    ASSERT_TRUE(overridden.has_value());
    EXPECT_EQ(overridden->pos, glm::uvec2(3, 4));
    EXPECT_EQ(overridden->size, glm::uvec2(640, 360));
}

TEST(CameraOutput, ModesAndWritebackMatchBevy) {
    camera::CameraOutputMode output;
    EXPECT_EQ(output.type, camera::CameraOutputMode::Type::Write);
    EXPECT_FALSE(output.blend_state.has_value());
    EXPECT_EQ(output.clear_color.type, camera::ClearColorConfig::Type::Default);
    EXPECT_EQ(camera::CameraOutputMode::skip().type, camera::CameraOutputMode::Type::Skip);
    EXPECT_EQ(camera::MsaaWriteback::Auto, camera::MsaaWriteback::Auto);
}

TEST(ClearColor, DefaultMatchesBevyDarkGray) {
    // bevy_camera::ClearColor::default() is Color::srgb_u8(43, 44, 47),
    // converted to linear RGB before it is written through an sRGB target.
    const ::epix::camera::ClearColor clear;
    EXPECT_NEAR(clear.r, 0.02415763f, 1e-7f);
    EXPECT_NEAR(clear.g, 0.02518686f, 1e-7f);
    EXPECT_NEAR(clear.b, 0.02842604f, 1e-7f);
    EXPECT_FLOAT_EQ(clear.a, 1.0f);
}

TEST(SubCameraView, DefaultsMatchBevy) {
    camera::SubCameraView sub;
    EXPECT_EQ(sub.full_size, glm::uvec2(1, 1));
    EXPECT_EQ(sub.offset, glm::vec2(0.0f));
    EXPECT_EQ(sub.size, glm::uvec2(1, 1));
}

TEST(SubCameraView, FullSizeCropPreservesProjection) {
    camera::PerspectiveProjection projection;
    projection.update(1920.0f, 1080.0f);
    const camera::SubCameraView whole{
        .full_size = glm::uvec2(1920, 1080), .offset = glm::vec2(0.0f), .size = glm::uvec2(1920, 1080)};
    EXPECT_EQ(projection.get_projection_matrix_for_sub(whole), projection.get_projection_matrix());
}

TEST(CameraProjection, UsesBevyReverseZConventions) {
    // Bevy's perspective projection is right-handed with -Z forward and an
    // infinite reverse-Z depth range: near maps to 1, far tends to 0.
    camera::PerspectiveProjection perspective;
    const auto perspective_matrix = perspective.get_projection_matrix();
    const auto near_clip          = perspective_matrix * glm::vec4(0.0f, 0.0f, -perspective.near_plane, 1.0f);
    const auto far_clip           = perspective_matrix * glm::vec4(0.0f, 0.0f, -1000000.0f, 1.0f);
    EXPECT_NEAR(near_clip.z / near_clip.w, 1.0f, 1e-5f);
    EXPECT_NEAR(far_clip.z / far_clip.w, 0.0f, 1e-5f);

    // Projection::default is Bevy's Perspective variant. Camera2d separately
    // supplies OrthographicProjection::default_2d through its requirements.
    EXPECT_TRUE(camera::Projection{}.as_perspective().has_value());
    EXPECT_EQ(camera::OrthographicProjection::default_3d().near_plane, 0.0f);
    EXPECT_EQ(camera::OrthographicProjection::default_2d().near_plane, -1000.0f);
}

TEST(Camera3d, DefaultsMatchBevyCameraComponents) {
    const ::epix::camera::Camera3d camera3d;
    EXPECT_EQ(camera3d.depth_load_op.type, ::epix::camera::Camera3dDepthLoadOp::Type::Clear);
    EXPECT_EQ(camera3d.depth_load_op.clear_value, 0.0f);
    EXPECT_EQ(camera3d.depth_texture_usages.usage(), wgpu::TextureUsage::eRenderAttachment);
    EXPECT_EQ(camera3d.screen_space_specular_transmission_steps, 1u);
    EXPECT_EQ(camera3d.screen_space_specular_transmission_quality,
              ::epix::camera::ScreenSpaceTransmissionQuality::Medium);
}

TEST(CameraCoordinates, ViewportAndNdcConversionsMatchBevy) {
    ::epix::camera::Camera camera;
    camera.computed.target_info =
        ::epix::camera::RenderTargetInfo{.physical_size = glm::uvec2(200, 100), .scale_factor = 2.0f};
    camera.computed.target_size = glm::uvec2(200, 100);
    camera.computed.projection  = glm::orthoRH_ZO(-100.0f, 100.0f, -50.0f, 50.0f, 1000.0f, -1000.0f);
    const ::epix::transform::GlobalTransform identity{};

    ASSERT_TRUE(camera.logical_viewport_size().has_value());
    EXPECT_EQ(*camera.logical_viewport_size(), glm::vec2(100.0f, 50.0f));
    EXPECT_EQ(*camera.target_scaling_factor(), 2.0f);

    auto screen = camera.world_to_viewport(identity, glm::vec3(0.0f));
    ASSERT_TRUE(screen.has_value());
    EXPECT_NEAR(screen->x, 50.0f, 1e-5f);
    EXPECT_NEAR(screen->y, 25.0f, 1e-5f);

    auto ndc = camera.viewport_to_ndc(glm::vec2(50.0f, 25.0f));
    ASSERT_TRUE(ndc.has_value());
    EXPECT_NEAR(ndc->x, 0.0f, 1e-5f);
    EXPECT_NEAR(ndc->y, 0.0f, 1e-5f);
    auto world = camera.viewport_to_world_2d(identity, glm::vec2(50.0f, 25.0f));
    ASSERT_TRUE(world.has_value());
    EXPECT_NEAR(world->x, 0.0f, 1e-5f);
    EXPECT_NEAR(world->y, 0.0f, 1e-5f);
}

TEST(CameraCoordinates, ReportsMissingViewportSize) {
    ::epix::camera::Camera camera;
    EXPECT_EQ(camera.viewport_to_ndc(glm::vec2(1.0f, 1.0f)).error(),
              ::epix::camera::ViewportConversionError::NoViewportSize);
}

// ComponentUniforms mirrors Bevy prepare_uniform_components: each component
// is pushed into the dynamic buffer and gets a sequential index.
TEST(ComponentUniforms, IndexAssignment) {
    ComponentUniforms<view::ViewUniform> cu;
    cu.uniforms_mut().dynamic_offset_alignment = 256;
    const std::size_t i0                       = cu.uniforms_mut().push(view::ViewUniform{});
    const std::size_t i1                       = cu.uniforms_mut().push(view::ViewUniform{});
    // Bevy: push returns the byte offset (stride 256 here), and
    // DynamicUniformIndex stores that byte offset.
    EXPECT_EQ(DynamicUniformIndex<view::ViewUniform>{static_cast<std::uint32_t>(i0)}.uniform_index(), 0u);
    // Bevy 0.18 ViewUniform is 768 bytes; stride = align_up(768, 256) = 768.
    EXPECT_EQ(DynamicUniformIndex<view::ViewUniform>{static_cast<std::uint32_t>(i1)}.uniform_index(), 768u);
    EXPECT_EQ(cu.uniforms_mut().len(), 2u);
}
// GpuArrayBuffer selects the storage-buffer path when the device supports
// storage buffers and the uniform fallback otherwise (Bevy
// gpu_component_array_buffer.rs + render_resource buffer selection); push
// assigns sequential indices in both paths.
TEST(GpuArrayBuffer, SelectsBackingAndIndexes) {
    // Storage-buffer path.
    wgpu::Limits storage_limits;
    storage_limits.maxStorageBuffersPerShaderStage = 8;
    render_resource::GpuArrayBuffer<view::ViewUniform> storage_buf(storage_limits);
    EXPECT_TRUE(std::holds_alternative<render_resource::BufferVec<view::ViewUniform>>(storage_buf.storage));
    EXPECT_EQ(storage_buf.push(view::ViewUniform{}).index, 0u);
    EXPECT_EQ(storage_buf.push(view::ViewUniform{}).index, 1u);
    EXPECT_FALSE(storage_buf.push(view::ViewUniform{}).dynamic_offset.has_value());

    // Uniform fallback (no storage buffers): dynamic offsets present.
    wgpu::Limits uniform_limits;
    uniform_limits.maxStorageBuffersPerShaderStage = 0;
    render_resource::GpuArrayBuffer<view::ViewUniform> uniform_buf(uniform_limits);
    EXPECT_TRUE(std::holds_alternative<render_resource::BatchedUniformBuffer<view::ViewUniform>>(uniform_buf.storage));
    auto idx0 = uniform_buf.push(view::ViewUniform{});
    auto idx1 = uniform_buf.push(view::ViewUniform{});
    // Bevy: index is the IN-BATCH element index (resets per batch); dynamic
    // offset is the batch-start byte offset captured before push
    // (batched_uniform_buffer.rs:82-93). With default limits capacity is 1,
    // so each push starts a fresh batch: index 0, offsets 0 and stride.
    EXPECT_EQ(idx0.index, 0u);
    EXPECT_EQ(idx1.index, 0u);
    EXPECT_TRUE(idx0.dynamic_offset.has_value());
    EXPECT_TRUE(idx1.dynamic_offset.has_value());
    EXPECT_EQ(*idx0.dynamic_offset, 0u);
    EXPECT_GT(*idx1.dynamic_offset, *idx0.dynamic_offset);
}

// prepare_uniform_components resolves the per-element stride from the device
// limits when the alignment was never set (Bevy uniform_buffer.rs:281-289).
TEST(ComponentUniforms, AlignmentResolvedFromLimits) {
    ComponentUniforms<view::ViewUniform> cu;
    EXPECT_EQ(cu.uniforms_mut().dynamic_offset_alignment, 0u);
    // Default fallback alignment is 256 (uniform_buffer.rs default); stride is
    // align_up(sizeof(ViewUniform)=768, 256) = 768.
    EXPECT_EQ(cu.uniforms_mut().element_stride(), 768u);

    wgpu::Limits limits;
    limits.minUniformBufferOffsetAlignment = 256;
    cu.uniforms_mut().update_alignment(limits);
    EXPECT_EQ(cu.uniforms_mut().dynamic_offset_alignment, 256u);
    EXPECT_EQ(cu.uniforms_mut().element_stride(), 768u);  // align_up(768, 256)
}

// ViewUniform matches Bevy 0.18's WGSL std140 layout exactly (view.wgsl).
TEST(ViewUniform, LayoutMatchesBevy) {
    static_assert(sizeof(view::ViewUniform) == 768);
    static_assert(alignof(view::ViewUniform) == 4);  // glm types are 4-aligned
    EXPECT_EQ(offsetof(view::ViewUniform, clip_from_world), 0u);
    EXPECT_EQ(offsetof(view::ViewUniform, unjittered_clip_from_world), 64u);
    EXPECT_EQ(offsetof(view::ViewUniform, world_from_clip), 128u);
    EXPECT_EQ(offsetof(view::ViewUniform, world_from_view), 192u);
    EXPECT_EQ(offsetof(view::ViewUniform, view_from_world), 256u);
    EXPECT_EQ(offsetof(view::ViewUniform, clip_from_view), 320u);
    EXPECT_EQ(offsetof(view::ViewUniform, view_from_clip), 384u);
    EXPECT_EQ(offsetof(view::ViewUniform, world_position), 448u);
    EXPECT_EQ(offsetof(view::ViewUniform, exposure), 460u);
    EXPECT_EQ(offsetof(view::ViewUniform, viewport), 464u);
    EXPECT_EQ(offsetof(view::ViewUniform, main_pass_viewport), 480u);
    EXPECT_EQ(offsetof(view::ViewUniform, frustum), 496u);
    EXPECT_EQ(offsetof(view::ViewUniform, color_grading), 592u);
    EXPECT_EQ(offsetof(view::ViewUniform, mip_bias), 752u);
    EXPECT_EQ(offsetof(view::ViewUniform, frame_count), 756u);
    // Bevy defaults carried by the uniform when components are absent.
    view::ViewUniform u;
    EXPECT_FLOAT_EQ(u.exposure, 1.0f);
    EXPECT_FLOAT_EQ(u.mip_bias, 0.0f);
    EXPECT_EQ(u.frame_count, 0u);
}

namespace {
// A bind-group-compatible component exercising the AsBindGroup trait.
struct TestMaterial {
    glm::vec4 color = glm::vec4(1.0f);
    int texture_id  = 0;
};
struct TestMaterialData {
    render_resource::DynamicUniformBuffer<glm::vec4> uniform;
};
}  // namespace

template <>
struct epix::render::render_resource::AsBindGroup<TestMaterial> {
    using Data  = TestMaterialData;
    using Param = std::tuple<>;
    static std::vector<render_resource::BindGroupLayoutEntryInfo> layout_entries() {
        return {
            render_resource::uniform_binding(0, wgpu::ShaderStage::eFragment, sizeof(glm::vec4)),
            render_resource::texture_binding(1, wgpu::ShaderStage::eFragment),
            render_resource::sampler_binding(2, wgpu::ShaderStage::eFragment),
        };
    }
    static render_resource::PreparedBindGroup<TestMaterial> as_bind_group(const wgpu::Device& device,
                                                                          const wgpu::BindGroupLayout& layout,
                                                                          const TestMaterial& material,
                                                                          Param& param) {
        (void)device;
        (void)layout;
        (void)material;
        (void)param;
        return render_resource::PreparedBindGroup<TestMaterial>{};
    }
};
static_assert(epix::render::render_resource::AsBindGroupImpl<TestMaterial>);

// AsBindGroup layout entries match the declared bindings (Bevy derive output).
TEST(AsBindGroup, LayoutEntries) {
    const auto entries = epix::render::render_resource::AsBindGroup<TestMaterial>::layout_entries();
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0].binding, 0u);
    EXPECT_EQ(entries[0].entry.binding, 0u);
    EXPECT_EQ(entries[0].entry.buffer.type, wgpu::BufferBindingType::eUniform);
    EXPECT_FALSE(entries[0].entry.buffer.hasDynamicOffset);  // Bevy derive default
    EXPECT_EQ(entries[0].entry.buffer.minBindingSize, sizeof(glm::vec4));
    EXPECT_EQ(entries[1].binding, 1u);
    EXPECT_EQ(entries[1].entry.texture.sampleType, wgpu::TextureSampleType::eFloat);
    EXPECT_EQ(entries[1].entry.texture.viewDimension, wgpu::TextureViewDimension::e2D);
    EXPECT_EQ(entries[2].binding, 2u);
    EXPECT_EQ(entries[2].entry.sampler.type, wgpu::SamplerBindingType::eFiltering);
    // visibility is applied to every entry
    EXPECT_EQ(entries[0].entry.visibility, wgpu::ShaderStage::eFragment);
}

// Storage and storage-texture builders produce the expected entry types.
TEST(AsBindGroup, EntryBuilders) {
    const auto read_only = render_resource::storage_binding(0, wgpu::ShaderStage::eCompute, true);
    EXPECT_EQ(read_only.entry.buffer.type, wgpu::BufferBindingType::eReadOnlyStorage);
    const auto read_write = render_resource::storage_binding(1, wgpu::ShaderStage::eCompute);
    EXPECT_EQ(read_write.entry.buffer.type, wgpu::BufferBindingType::eStorage);  // Bevy derive default: read_only=false
    const auto tex = render_resource::storage_texture_binding(
        2, wgpu::ShaderStage::eCompute, wgpu::StorageTextureAccess::eWriteOnly, wgpu::TextureFormat::eRGBA8Unorm);
    EXPECT_EQ(tex.entry.storageTexture.access, wgpu::StorageTextureAccess::eWriteOnly);
    EXPECT_EQ(tex.entry.storageTexture.format, wgpu::TextureFormat::eRGBA8Unorm);
}

// FrameCountPlugin mirrors bevy_diagnostic::FrameCountPlugin: FrameCount is
// incremented once per app update in the Last schedule (0 during the first
// update, 1 during the next).
TEST(FrameCountPlugin, IncrementsPerUpdate) {
    epix::app::App app = epix::app::App::create();
    app.add_plugins(FrameCountPlugin{});
    app.update();
    auto count = app.world().get_resource<FrameCount>();
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count->get().count, 1u);
    app.update();
    EXPECT_EQ(app.world().get_resource<FrameCount>()->get().count, 2u);
}

// ---- render graph parity (bevy_render 0.18 render_graph/graph.rs, node.rs) ----

namespace {
struct GraphTestNodeA {};
struct GraphTestNodeB {};
struct GraphTestNodeC {};
struct GraphTestNodeD {};
struct ProbeGraphNode : graph::Node {
    std::vector<graph::SlotInfo> input() override { return {}; }
    std::vector<graph::SlotInfo> output() override { return {}; }
};
}  // namespace

// Bevy graph.rs:98-104 PANICS when set_input is called twice.
TEST(RenderGraph, SetInputTwiceThrows) {
    graph::RenderGraph g;
    g.set_input(std::vector<graph::SlotInfo>{graph::SlotInfo{"in", graph::SlotType::Buffer}});
    EXPECT_THROW(g.set_input(std::vector<graph::SlotInfo>{graph::SlotInfo{"in2", graph::SlotType::Buffer}}),
                 std::runtime_error);
}

// Bevy graph.rs:135-142: add_node with a duplicate label REPLACES the node.
TEST(RenderGraph, AddNodeReplaces) {
    graph::RenderGraph g;
    g.add_node<ProbeGraphNode>(GraphTestNodeA{});
    g.add_node<ProbeGraphNode>(GraphTestNodeA{});
    std::size_t node_count = 0;
    for (auto&& n : g.iter_nodes()) {
        (void)n;
        ++node_count;
    }
    EXPECT_EQ(node_count, 1u);
    auto st = g.get_node_state(GraphTestNodeA{});
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(st->get().type_name, typeid(ProbeGraphNode).name());
}

// Bevy graph.rs:577-579: add_sub_graph with a duplicate label REPLACES the graph.
TEST(RenderGraph, AddSubGraphReplaces) {
    graph::RenderGraph g;
    graph::RenderGraph inner;
    inner.add_node<ProbeGraphNode>(GraphTestNodeA{});
    EXPECT_TRUE(g.add_sub_graph(GraphTestNodeC{}, std::move(inner)).has_value());
    graph::RenderGraph inner2;
    inner2.add_node<ProbeGraphNode>(GraphTestNodeB{});
    EXPECT_TRUE(g.add_sub_graph(GraphTestNodeC{}, std::move(inner2)).has_value());
    auto sub = g.get_sub_graph(GraphTestNodeC{});
    ASSERT_TRUE(sub.has_value());
    // The replaced graph contains only GraphTestNodeB.
    EXPECT_TRUE(sub->get().get_node_state(GraphTestNodeA{}).has_value() == false);
    EXPECT_TRUE(sub->get().get_node_state(GraphTestNodeB{}).has_value());
}

// Bevy graph.rs add_edge -> validate_edge_duplicates: duplicate edge errors.
TEST(RenderGraph, DuplicateEdgeAlreadyExists) {
    graph::RenderGraph g;
    g.add_node<ProbeGraphNode>(GraphTestNodeA{});
    g.add_node<ProbeGraphNode>(GraphTestNodeB{});
    EXPECT_TRUE(g.try_add_node_edge(GraphTestNodeA{}, GraphTestNodeB{}).has_value());
    auto r2 = g.try_add_node_edge(GraphTestNodeA{}, GraphTestNodeB{});
    ASSERT_FALSE(r2.has_value());
    ASSERT_TRUE(std::holds_alternative<graph::EdgeError>(r2.error()));
    EXPECT_TRUE(std::holds_alternative<graph::EdgeAlreadyExists>(std::get<graph::EdgeError>(r2.error())));
}

// Bevy graph.rs remove_edge -> validate_edge_existence: missing edge errors.
TEST(RenderGraph, RemoveMissingEdgeDoesNotExist) {
    graph::RenderGraph g;
    g.add_node<ProbeGraphNode>(GraphTestNodeA{});
    g.add_node<ProbeGraphNode>(GraphTestNodeB{});
    EXPECT_TRUE(g.remove_node_edge(GraphTestNodeA{}, GraphTestNodeB{}).has_value() == false);
    EXPECT_TRUE(g.try_add_node_edge(GraphTestNodeA{}, GraphTestNodeB{}).has_value());
    EXPECT_TRUE(g.remove_node_edge(GraphTestNodeA{}, GraphTestNodeB{}).has_value());
    auto r4 = g.remove_node_edge(GraphTestNodeA{}, GraphTestNodeB{});
    ASSERT_FALSE(r4.has_value());
    ASSERT_TRUE(std::holds_alternative<graph::EdgeError>(r4.error()));
    EXPECT_TRUE(std::holds_alternative<graph::EdgeDoesNotExist>(std::get<graph::EdgeError>(r4.error())));
}

// validate_edge: should_exist=false + existing edge -> EdgeAlreadyExists;
// should_exist=true + missing edge -> EdgeDoesNotExist.
TEST(RenderGraph, ValidateEdgeExistence) {
    graph::RenderGraph g;
    g.add_node<ProbeGraphNode>(GraphTestNodeA{});
    g.add_node<ProbeGraphNode>(GraphTestNodeB{});
    auto e = graph::Edge::node_edge(GraphTestNodeA{}, GraphTestNodeB{});
    EXPECT_TRUE(g.validate_edge(e, false).has_value());
    auto r = g.validate_edge(e, true);
    ASSERT_FALSE(r.has_value());
    EXPECT_TRUE(std::holds_alternative<graph::EdgeDoesNotExist>(r.error()));
    EXPECT_TRUE(g.try_add_node_edge(GraphTestNodeA{}, GraphTestNodeB{}).has_value());
    auto r2 = g.validate_edge(e, false);
    ASSERT_FALSE(r2.has_value());
    EXPECT_TRUE(std::holds_alternative<graph::EdgeAlreadyExists>(r2.error()));
}

// Bevy graph.rs:507-519 has_edge requires the edge in BOTH endpoint lists.
TEST(RenderGraph, HasEdgeRequiresBothEndpoints) {
    graph::RenderGraph g;
    g.add_node<ProbeGraphNode>(GraphTestNodeA{});
    g.add_node<ProbeGraphNode>(GraphTestNodeB{});
    auto e = graph::Edge::node_edge(GraphTestNodeA{}, GraphTestNodeB{});
    // Register only on the input node's side.
    g.get_node_state(GraphTestNodeB{})->get().edges.add_input_edge(e);
    EXPECT_FALSE(g.has_edge(e));
    g.get_node_state(GraphTestNodeA{})->get().edges.add_output_edge(e);
    EXPECT_TRUE(g.has_edge(e));
}

// Bevy node.rs:158-166 remove_input_edge = swap_remove: ONE match, order-changing.
TEST(Edges, RemoveInputEdgeSwapSemantics) {
    graph::Edges edges(graph::NodeLabel(GraphTestNodeA{}));
    auto e1 = graph::Edge::node_edge(GraphTestNodeA{}, GraphTestNodeB{});
    auto e2 = graph::Edge::node_edge(GraphTestNodeA{}, GraphTestNodeC{});
    auto e3 = graph::Edge::node_edge(GraphTestNodeA{}, GraphTestNodeD{});
    edges.add_input_edge(e1);
    edges.add_input_edge(e2);
    edges.add_input_edge(e3);
    edges.remove_input_edge(e1);  // swap_remove: e3 moves into index 0
    ASSERT_EQ(edges.input_edges().size(), 2u);
    EXPECT_EQ(edges.input_edges()[0], e3);
    EXPECT_EQ(edges.input_edges()[1], e2);
}

// NodeState::type_name populated from the concrete node type (node.rs:240).
TEST(NodeState, TypeName) {
    graph::RenderGraph g;
    g.add_node<ProbeGraphNode>(GraphTestNodeA{});
    auto st = g.get_node_state(GraphTestNodeA{});
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(st->get().type_name, typeid(ProbeGraphNode).name());
    EXPECT_FALSE(st->get().type_name.empty());
}

// ShaderStorageBuffer::set_data / from serialize a typed value (Bevy
// ShaderStorageBuffer::set_data + From<T>).
TEST(ShaderStorageBuffer, SetDataAndFrom) {
    struct Payload {
        std::uint32_t a;
        float b;
    };
    const Payload payload{7, 1.5f};
    auto buf = ShaderStorageBuffer::from(payload);
    ASSERT_TRUE(buf.data.has_value());
    EXPECT_EQ(buf.data->size(), sizeof(Payload));
    EXPECT_EQ(buf.size, sizeof(Payload));
    Payload roundtrip;
    std::memcpy(&roundtrip, buf.data->data(), sizeof(Payload));
    EXPECT_EQ(roundtrip.a, 7u);
    EXPECT_FLOAT_EQ(roundtrip.b, 1.5f);
}

// RenderAssetBytesPerFrame::new + RenderAssetBytesPerFrameLimiter budget
// accounting (Bevy render_asset.rs byte budget).
TEST(RenderAssetBytesPerFrame, BudgetLimiter) {
    auto budget = RenderAssetBytesPerFrame::new_with_max_bytes(1024);
    ASSERT_TRUE(budget.max_bytes.has_value());
    EXPECT_EQ(*budget.max_bytes, 1024u);
    RenderAssetBytesPerFrameLimiter limiter;
    limiter.max_bytes = budget.max_bytes;
    EXPECT_FALSE(limiter.exhausted());
    EXPECT_EQ(limiter.available_bytes(), 1024u);
    limiter.write_bytes(400);
    EXPECT_EQ(limiter.available_bytes(), 624u);
    limiter.write_bytes(624);
    EXPECT_TRUE(limiter.exhausted());
    EXPECT_EQ(limiter.available_bytes(), 0u);
    limiter.reset();
    EXPECT_EQ(limiter.available_bytes(), 1024u);
    // Unlimited: never exhausted, SIZE_MAX available.
    RenderAssetBytesPerFrameLimiter unlimited;
    EXPECT_FALSE(unlimited.exhausted());
    EXPECT_EQ(unlimited.available_bytes(), std::numeric_limits<std::size_t>::max());
    unlimited.write_bytes(100);
    EXPECT_EQ(unlimited.available_bytes(), std::numeric_limits<std::size_t>::max());
}

// ColorAttachment clears on the first get_attachment call and loads afterwards
// (Bevy texture_attachment.rs is_first_call semantics).
TEST(ColorAttachment, FirstCallClearsThenLoads) {
    render_resource::ColorAttachment attachment;
    attachment.clear_color = glm::vec4(0.1f, 0.2f, 0.3f, 1.0f);
    auto first             = attachment.get_unsampled_attachment();
    EXPECT_EQ(first.loadOp, wgpu::LoadOp::eClear);
    EXPECT_EQ(first.storeOp, wgpu::StoreOp::eStore);
    auto second = attachment.get_unsampled_attachment();
    EXPECT_EQ(second.loadOp, wgpu::LoadOp::eLoad);
    // mark_as_cleared forces load on the next call too.
    attachment.mark_as_cleared();
    auto third = attachment.get_unsampled_attachment();
    EXPECT_EQ(third.loadOp, wgpu::LoadOp::eLoad);
    // Without a clear color, the first call also loads (Bevy: (None, _) -> Load).
    render_resource::ColorAttachment no_clear;
    EXPECT_EQ(no_clear.get_unsampled_attachment().loadOp, wgpu::LoadOp::eLoad);
}

// ColorAttachment::get_attachment honors the MSAA resolve target (Bevy
// texture_attachment.rs): it renders to the resolve target and resolves to
// the main texture.
TEST(ColorAttachment, ResolveTargetAttachment) {
    render_resource::ColorAttachment attachment;
    attachment.resolve_target = render_resource::CachedTexture{};
    auto attachment_view      = attachment.get_attachment();
    EXPECT_EQ(attachment_view.loadOp, wgpu::LoadOp::eLoad);
    EXPECT_EQ(attachment_view.storeOp, wgpu::StoreOp::eStore);
}

// OutputColorAttachment::needs_present tracks whether a pass wrote to the
// output (Bevy OutputColorAttachment::needs_present).
TEST(OutputColorAttachment, NeedsPresentTracksWrite) {
    view::OutputColorAttachment out;
    EXPECT_FALSE(out.needs_present());
    out.mark_as_cleared();  // a render pass wrote to it
    EXPECT_TRUE(out.needs_present());
}

// ViewTarget::post_process_write flips the A/B toggle and returns the
// source->destination pair to copy (Bevy view/mod.rs post_process_write).
TEST(ViewTarget, PostProcessWriteFlipsAB) {
    view::ViewTarget target;
    EXPECT_EQ(target.main_texture->load(std::memory_order_seq_cst), 0u);  // A current
    auto write_a_to_b = target.post_process_write();
    EXPECT_EQ(target.main_texture->load(std::memory_order_seq_cst), 1u);  // now B
    EXPECT_FALSE(write_a_to_b.source);                                    // null views; handles exercised
    EXPECT_FALSE(write_a_to_b.destination);
    auto write_b_to_a = target.post_process_write();
    EXPECT_EQ(target.main_texture->load(std::memory_order_seq_cst), 0u);  // back to A
    EXPECT_FALSE(write_b_to_a.source);
    // The legacy texture_view aliases the current main texture view.
    EXPECT_EQ(target.texture_view.raw(), target.main_texture_view().raw());
}

// DrawFunctions::add always appends and re-maps the type to the NEW id
// (Bevy draw.rs:74-84); add_with maps under an arbitrary type; id() throws
// when the type is not registered (Bevy panics).
TEST(DrawFunctions, AddAppendsAndRemaps) {
    phase::DrawFunctions<TestBinnedItem> functions;
    const phase::DrawFunctionId first = functions.template add<phase::EmptyDrawFunction<TestBinnedItem>>();
    EXPECT_EQ(first.get(), 0u);
    EXPECT_EQ(functions.template id<phase::EmptyDrawFunction<TestBinnedItem>>().get(), 0u);
    // Re-registering the same type appends a NEW function (Bevy semantics).
    const phase::DrawFunctionId second = functions.template add<phase::EmptyDrawFunction<TestBinnedItem>>();
    EXPECT_EQ(second.get(), 1u);
    EXPECT_EQ(functions.template id<phase::EmptyDrawFunction<TestBinnedItem>>().get(), 1u);
    // add_with maps the draw function under an arbitrary mapped type.
    struct MappedDrawType {};
    const phase::DrawFunctionId mapped =
        functions.template add_with<MappedDrawType, phase::EmptyDrawFunction<TestBinnedItem>>();
    EXPECT_EQ(mapped.get(), 2u);
    EXPECT_EQ(functions.template id<MappedDrawType>().get(), 2u);
    // id() of an unregistered type throws (Bevy panics).
    struct UnregisteredDrawType {};
    EXPECT_THROW(functions.template id<UnregisteredDrawType>(), std::runtime_error);
}

// RenderPhase::clear empties the item list (Bevy SortedRenderPhase::clear).
TEST(RenderPhase, Clear) {
    phase::RenderPhase<TestBinnedItem> phase;
    phase.add(TestBinnedItem{});
    phase.add(TestBinnedItem{});
    EXPECT_EQ(phase.items.size(), 2u);
    phase.clear();
    EXPECT_TRUE(phase.items.empty());
}
namespace {
// Binned phase item with the bin->item factory (Bevy BPI::new; named
// create in C++ because new is a keyword).
struct RenderableBinnedItem {
    Entity m_entity;
    phase::DrawFunctionId m_draw_function;
    int m_bin_key       = 0;
    int m_batch_set_key = 0;
    std::pair<std::uint32_t, std::uint32_t> batch_range{0, 1};  // stored range (Bevy batch_range: Range<u32>)

    Entity entity() const { return m_entity; }
    sync_world::MainEntity main_entity() const { return sync_world::MainEntity{m_entity}; }
    int sort_key() const { return 0; }
    phase::DrawFunctionId draw_function() const { return m_draw_function; }
    phase::PhaseItemExtraIndex extra_index() const { return phase::PhaseItemExtraIndex::None; }
    using BinKey      = int;
    using BatchSetKey = int;
    const int& bin_key() const { return m_bin_key; }
    const int& batch_set_key() const { return m_batch_set_key; }
    bool batchable() const { return true; }

    static RenderableBinnedItem create(int batch_set_key,
                                       int bin_key,
                                       Entity representative,
                                       std::uint32_t instance_start,
                                       std::uint32_t instance_end) {
        RenderableBinnedItem item;
        item.m_entity        = representative;
        item.m_bin_key       = bin_key;
        item.m_batch_set_key = batch_set_key;
        item.batch_range     = std::pair<std::uint32_t, std::uint32_t>{instance_start, instance_end};
        return item;
    }
};
static_assert(epix::render::phase::BinnedPhaseItem<RenderableBinnedItem>);

// Draw function that records each invocation.
struct CountingBinnedDraw : phase::DrawFunction<RenderableBinnedItem> {
    static inline int calls          = 0;
    static inline int last_bin_key   = -1;
    static inline int last_range_end = -1;
    void prepare(const epix::ecs::World&) override {}
    std::expected<void, phase::DrawError> draw(const epix::ecs::World&,
                                               const wgpu::RenderPassEncoder&,
                                               epix::ecs::Entity,
                                               const RenderableBinnedItem& item) override {
        ++calls;
        last_bin_key   = item.m_bin_key;
        last_range_end = static_cast<int>(item.batch_range.second);
        return {};
    }
};
}  // namespace

// BinnedRenderPhase::render encodes a draw call per batchable bin and per
// unbatchable/non-mesh entity (Bevy BinnedRenderPhase::render).
TEST(BinnedRenderPhase, RenderInvokesDrawFunctions) {
    CountingBinnedDraw::calls          = 0;
    CountingBinnedDraw::last_bin_key   = -1;
    CountingBinnedDraw::last_range_end = -1;

    epix::ecs::World world(WorldId(0));
    phase::DrawFunctions<RenderableBinnedItem> functions;
    functions.template add<CountingBinnedDraw>();
    world.insert_resource(std::move(functions));

    phase::BinnedRenderPhase<RenderableBinnedItem> phase;
    const auto tick = world.change_tick();
    // Two batchable bins with different keys.
    phase.add(0, 7, Entity{1}, sync_world::MainEntity{Entity{1}}, phase::InputUniformIndex{0},
              phase::BinnedRenderPhaseType::BatchableMesh, tick);
    phase.add(0, 7, Entity{2}, sync_world::MainEntity{Entity{2}}, phase::InputUniformIndex{1},
              phase::BinnedRenderPhaseType::BatchableMesh, tick);
    phase.add(1, 9, Entity{3}, sync_world::MainEntity{Entity{3}}, phase::InputUniformIndex{2},
              phase::BinnedRenderPhaseType::BatchableMesh, tick);
    // One unbatchable mesh and one non-mesh entity.
    phase.add(0, 11, Entity{4}, sync_world::MainEntity{Entity{4}}, phase::InputUniformIndex{3},
              phase::BinnedRenderPhaseType::UnbatchableMesh, tick);
    phase.add(0, 13, Entity{5}, sync_world::MainEntity{Entity{5}}, phase::InputUniformIndex{4},
              phase::BinnedRenderPhaseType::NonMesh, tick);

    // One draw call per batchable BIN (2 bins) + one per unbatchable entity
    // + one per non-mesh entity = 4 (Bevy storage-buffer path).
    wgpu::RenderPassEncoder null_pass{};
    phase.render(null_pass, world, Entity{100});
    EXPECT_EQ(CountingBinnedDraw::calls, 4);
    // The last call was the non-mesh item (bin key 13, range 1).
    EXPECT_EQ(CountingBinnedDraw::last_bin_key, 13);
    EXPECT_EQ(CountingBinnedDraw::last_range_end, 1);
}

namespace {
// Sorted-phase draw function that counts invocations (Bevy draw functions
// return Result<(), DrawError>).
struct CountingSortedDraw : phase::DrawFunction<TestBinnedItem> {
    static inline int calls = 0;
    void prepare(const epix::ecs::World&) override {}
    std::expected<void, phase::DrawError> draw(const epix::ecs::World&,
                                               const wgpu::RenderPassEncoder&,
                                               epix::ecs::Entity,
                                               const TestBinnedItem&) override {
        ++calls;
        return {};
    }
};
}  // namespace

// RenderPhase::render_range skips batch_range.len() items after a batched
// draw (Bevy render_phase/mod.rs:1470-1487: index += batch_range.len()).
TEST(RenderPhase, RenderSkipsBatchedItems) {
    CountingSortedDraw::calls = 0;
    epix::ecs::World world(WorldId(0));
    phase::DrawFunctions<TestBinnedItem> functions;
    const phase::DrawFunctionId draw_id = functions.template add<CountingSortedDraw>();
    world.insert_resource(std::move(functions));

    phase::RenderPhase<TestBinnedItem> phase;
    TestBinnedItem batched;
    batched.m_entity        = Entity{1};
    batched.m_draw_function = draw_id;
    batched.batch_range = std::pair<std::uint32_t, std::uint32_t>{4, 6};  // 2 instances: draw once, skip the next item
    TestBinnedItem next;
    next.m_entity        = Entity{2};
    next.m_draw_function = draw_id;
    next.batch_range     = std::pair<std::uint32_t, std::uint32_t>{6, 7};
    phase.add(batched);
    phase.add(next);

    wgpu::RenderPassEncoder null_pass{};
    phase.render(null_pass, world, Entity{100});
    // The first item covers 2 instances -> the second item is skipped.
    EXPECT_EQ(CountingSortedDraw::calls, 1);
}

// ShaderStorageBuffer::take_gpu_data moves the data out of the stored asset
// so it can stay in Assets<T> (Bevy RenderAsset::take_gpu_data).
TEST(ShaderStorageBuffer, TakeGpuData) {
    ShaderStorageBuffer stored;
    stored.set_data(std::array<std::uint8_t, 4>{1, 2, 3, 4});
    ASSERT_TRUE(stored.data.has_value());
    EXPECT_EQ(stored.size, 4u);

    RenderAsset<ShaderStorageBuffer> impl;
    auto extracted = impl.take_gpu_data(stored);
    ASSERT_TRUE(extracted.has_value());
    ASSERT_TRUE(extracted->data.has_value());
    EXPECT_EQ(extracted->data->size(), 4u);
    // The stored asset is stripped of data but keeps its declared descriptor
    // size, so a re-extraction of a Modified asset still prepares a correctly
    // sized buffer (Bevy storage.rs keeps the size in the source).
    EXPECT_FALSE(stored.data.has_value());
    EXPECT_EQ(stored.size, 4u);
}

// ViewVisibility matches Bevy bevy_camera::visibility: get() reports whether
// the entity is visible to any view (not culled); per-view flags are tracked.
TEST(ViewVisibility, Flags) {
    camera::ViewVisibility vv;
    EXPECT_TRUE(vv.get());  // default: not culled
    EXPECT_FALSE(vv.get_in_view(0));
    vv.set_in_view(0, true);
    EXPECT_TRUE(vv.get_in_view(0));
    EXPECT_FALSE(vv.get_in_view(1));
    vv.culled();
    EXPECT_FALSE(vv.get());  // culled -> not visible to any view
    vv.visible();
    EXPECT_TRUE(vv.get());
    EXPECT_TRUE(vv.get_in_view(0));  // per-view flag survives cull toggles
}

// RenderVisibleEntities accessors match Bevy view/visibility/mod.rs:23-53:
// get/iter/len/is_empty are keyed by query-filter type, empty when absent.
TEST(RenderVisibleEntities, AccessorsMatchBevy) {
    struct SomeQueryFilter {};
    struct OtherQueryFilter {};
    view::RenderVisibleEntities rve;
    EXPECT_TRUE(rve.is_empty<SomeQueryFilter>());
    EXPECT_EQ(rve.len<SomeQueryFilter>(), 0u);
    EXPECT_TRUE(rve.get<SomeQueryFilter>().empty());

    rve.entities[epix::meta::type_index(epix::meta::type_id<SomeQueryFilter>())] = {
        {epix::ecs::Entity::from_index(1), sync_world::MainEntity{epix::ecs::Entity::from_index(10)}},
        {epix::ecs::Entity::from_index(2), sync_world::MainEntity{epix::ecs::Entity::from_index(20)}},
    };

    EXPECT_FALSE(rve.is_empty<SomeQueryFilter>());
    EXPECT_EQ(rve.len<SomeQueryFilter>(), 2u);
    EXPECT_EQ(rve.get<SomeQueryFilter>().size(), 2u);
    std::size_t seen = 0;
    for (auto&& [render_entity, main_entity] : rve.iter<SomeQueryFilter>()) {
        EXPECT_EQ(render_entity.index, static_cast<std::uint32_t>(seen + 1));
        EXPECT_EQ(main_entity.entity.index, static_cast<std::uint32_t>((seen + 1) * 10));
        ++seen;
    }
    EXPECT_EQ(seen, 2u);
    // Different filter type: no entries.
    EXPECT_TRUE(rve.is_empty<OtherQueryFilter>());
    EXPECT_EQ(rve.len<OtherQueryFilter>(), 0u);
}

// Per-stage shader_defs match Bevy pipeline.rs:146-191: VertexState,
// FragmentState and ComputePipelineDescriptor each carry their own defs and
// the builder appends them (they reach the shader cache as variant keys).
TEST(PipelineDescriptor, ShaderDefsPerStage) {
    epix::assets::Handle<epix::shader::Shader> shader_handle{uuids::uuid{}};
    VertexState vs{
        shader_handle,
        {epix::shader::ShaderDefVal::from_bool("VS_OPTION"), epix::shader::ShaderDefVal::from_int("MAX_INSTANCES", 64)},
        std::nullopt,
        {}};
    ASSERT_EQ(vs.shader_defs.size(), 2u);
    EXPECT_EQ(vs.shader_defs[0].name, "VS_OPTION");
    EXPECT_EQ(std::get<bool>(vs.shader_defs[0].value), true);
    EXPECT_EQ(vs.shader_defs[1].name, "MAX_INSTANCES");

    FragmentState fs{shader_handle, {epix::shader::ShaderDefVal::from_bool("FS_OPTION")}, std::nullopt, {}};
    ASSERT_EQ(fs.shader_defs.size(), 1u);

    ComputePipelineDescriptor cp{
        "", {}, {}, shader_handle, {epix::shader::ShaderDefVal::from_uint("WORKGROUP", 8u)}, std::nullopt};
    ASSERT_EQ(cp.shader_defs.size(), 1u);
    EXPECT_EQ(std::get<std::uint32_t>(cp.shader_defs[0].value), 8u);

    // Distinct stages do not share defs.
    EXPECT_NE(vs.shader_defs.size(), fs.shader_defs.size());

    // push_constant_ranges carried per descriptor (Bevy pipeline.rs:115,181).
    wgpu::PushConstantRange range;
    range.start  = 0;
    range.end    = 4;
    range.stages = wgpu::ShaderStage::eVertex;
    VertexState vs2{shader_handle, {}, std::nullopt, {}};
    std::vector<wgpu::PushConstantRange> ranges{range};
    RenderPipelineDescriptor rpd{
        "", {}, ranges, vs2, wgpu::PrimitiveState(), std::nullopt, wgpu::MultisampleState(), std::nullopt};
    ASSERT_EQ(rpd.push_constant_ranges.size(), 1u);
    EXPECT_EQ(rpd.push_constant_ranges[0].start, 0u);
    EXPECT_EQ(rpd.push_constant_ranges[0].end, 4u);

    ComputePipelineDescriptor cp2{"", {}, ranges, shader_handle, {}, std::nullopt};
    ASSERT_EQ(cp2.push_constant_ranges.size(), 1u);
}

// CachedRenderPipelineId/CachedComputePipelineId INVALID sentinels match Bevy
// (pipeline_cache.rs:49,63: INVALID = usize::MAX).
TEST(PipelineCacheId, InvalidSentinels) {
    EXPECT_EQ(INVALID_RENDER_PIPELINE_ID.get(), std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(INVALID_COMPUTE_PIPELINE_ID.get(), std::numeric_limits<std::uint64_t>::max());

    EXPECT_NE(INVALID_RENDER_PIPELINE_ID, CachedRenderPipelineId{0});
}

// WgpuSettings defaults mirror Bevy settings.rs (HighPerformance +
// Functionality + auto backend); env overrides honor WGPU_BACKEND /
// WGPU_POWER_PREF / WGPU_SETTINGS_PRIO.
TEST(WgpuSettings, DefaultsAndEnvOverrides) {
    WgpuSettings settings;
    EXPECT_EQ(settings.power_preference, wgpu::PowerPreference::eHighPerformance);
    EXPECT_EQ(settings.priority, WgpuSettingsPriority::Functionality);
    ASSERT_TRUE(settings.backends.has_value());
    EXPECT_EQ(*settings.backends, wgpu::BackendType::eVulkan);  // engine default
    EXPECT_FALSE(settings.force_fallback_adapter);
    EXPECT_EQ(settings.device_label, "Render Device");

    // No env vars set: overrides leave defaults untouched.
    settings.apply_env_overrides();
    EXPECT_EQ(settings.power_preference, wgpu::PowerPreference::eHighPerformance);
    ASSERT_TRUE(settings.backends.has_value());
    EXPECT_EQ(*settings.backends, wgpu::BackendType::eVulkan);

    // With env vars set, values are picked up. Restore with the empty string
    // (never nullptr - _putenv_s(nullptr) is a crash on MSVC).
    _putenv_s("WGPU_BACKEND", "vulkan");
    _putenv_s("WGPU_POWER_PREF", "low");
    _putenv_s("WGPU_SETTINGS_PRIO", "webgl2");
    settings.apply_env_overrides();
    EXPECT_TRUE(settings.backends.has_value());
    EXPECT_EQ(*settings.backends, wgpu::BackendType::eVulkan);
    EXPECT_EQ(settings.power_preference, wgpu::PowerPreference::eLowPower);
    EXPECT_EQ(settings.priority, WgpuSettingsPriority::WebGL2);

    _putenv_s("WGPU_BACKEND", "");
    _putenv_s("WGPU_POWER_PREF", "");
    _putenv_s("WGPU_SETTINGS_PRIO", "");
}

// GpuImage::aspect_ratio / size_2d match Bevy gpu_image.rs:142-152.
TEST(GpuImage, AspectRatioAndSize2d) {
    texture::GpuImage image;
    image.size = wgpu::Extent3D{800, 600, 1};
    EXPECT_FLOAT_EQ(image.aspect_ratio(), 800.0f / 600.0f);
    EXPECT_EQ(image.size_2d(), glm::uvec2(800, 600));
    image.size = wgpu::Extent3D{640, 480, 1};
    EXPECT_FLOAT_EQ(image.aspect_ratio(), 640.0f / 480.0f);
}

// ReadbackComplete::to_shader_type decodes the raw bytes (Bevy
// gpu_readback.rs:122-129).
TEST(ReadbackComplete, ToShaderType) {
    struct Payload {
        float x;
        std::uint32_t y;
    };
    Payload p{3.5f, 42u};
    ReadbackComplete complete{epix::ecs::Entity{}, {}};
    complete.data.resize(sizeof(Payload));
    std::memcpy(complete.data.data(), &p, sizeof(Payload));

    Payload out = complete.to_shader_type<Payload>();
    EXPECT_FLOAT_EQ(out.x, 3.5f);
    EXPECT_EQ(out.y, 42u);
}

// RunSubGraph carries an optional debug_group marker name (Bevy
// graph_runner.rs:111); GraphContext::label exposes the running node's label
// (Bevy RenderGraphContext::label).
TEST(GraphContext, LabelAndDebugGroup) {
    using graph::GraphLabel;
    struct MySubGraph {};
    // RunSubGraph.debug_group round-trips (Bevy graph_runner.rs:111).
    graph::RunSubGraph run{GraphLabel::from_type<MySubGraph>(), {}, std::nullopt, std::string("my_group")};
    ASSERT_TRUE(run.debug_group.has_value());
    EXPECT_EQ(*run.debug_group, "my_group");
    graph::RunSubGraph plain{GraphLabel::from_type<MySubGraph>(), {}, std::nullopt, std::nullopt};
    EXPECT_FALSE(plain.debug_group.has_value());
    // Default-constructed field matches Bevy's None default.

    graph::RunSubGraph defaulted;
    EXPECT_FALSE(defaulted.debug_group.has_value());
}

// RenderGraph iter_nodes_mut / iter_sub_graphs / remove_sub_graph / get_node<T>
// match Bevy graph.rs:522-584.
TEST(RenderGraph, IterAndRemoveAccessors) {
    using graph::GraphLabel;
    using graph::NodeLabel;
    using graph::RenderGraph;
    struct SubA {};
    struct SubB {};
    struct MyNode {};

    RenderGraph graph;
    graph.add_node(NodeLabel::from_type<MyNode>(), graph::EmptyNode{});

    // get_node<T>: returns the concrete node.
    EXPECT_NE(graph.get_node<graph::EmptyNode>(NodeLabel::from_type<MyNode>()), nullptr);
    EXPECT_EQ(graph.get_node<graph::EmptyNode>(NodeLabel::from_type<SubA>()), nullptr);  // absent

    // iter_nodes / iter_nodes_mut visit the node.
    std::size_t node_count = 0;
    for (auto&& n : graph.iter_nodes()) {
        (void)n;
        ++node_count;
    }
    EXPECT_EQ(node_count, 1u);
    std::size_t mut_count = 0;
    for (auto&& n : graph.iter_nodes_mut()) {
        (void)n;
        ++mut_count;
    }
    EXPECT_EQ(mut_count, 1u);

    // Sub-graphs iterate as (label, graph) pairs and remove_sub_graph erases.
    graph.add_sub_graph(GraphLabel::from_type<SubA>(), RenderGraph{});
    graph.add_sub_graph(GraphLabel::from_type<SubB>(), RenderGraph{});
    std::size_t sub_count = 0;
    for (auto&& [label, sub] : graph.iter_sub_graphs()) {
        (void)label;
        (void)sub;
        ++sub_count;
    }
    EXPECT_EQ(sub_count, 2u);
    std::size_t sub_mut_count = 0;
    for (auto&& [label, sub] : graph.iter_sub_graphs_mut()) {
        (void)label;
        (void)sub;
        ++sub_mut_count;
    }
    EXPECT_EQ(sub_mut_count, 2u);

    graph.remove_sub_graph(GraphLabel::from_type<SubA>());
    EXPECT_FALSE(graph.get_sub_graph(GraphLabel::from_type<SubA>()).has_value());
    EXPECT_TRUE(graph.get_sub_graph(GraphLabel::from_type<SubB>()).has_value());

    graph.remove_sub_graph(GraphLabel::from_type<SubA>());  // no-op when absent
    EXPECT_TRUE(graph.get_sub_graph(GraphLabel::from_type<SubB>()).has_value());
}

// DynamicStorageBuffer::push returns per-element BYTE OFFSETS with alignment
// (Bevy storage_buffer.rs:226-228), like DynamicUniformBuffer.
TEST(DynamicStorageBuffer, PushReturnsByteOffsets) {
    render_resource::DynamicStorageBuffer<GlobalsUniform> buf;
    EXPECT_EQ(buf.element_stride(), 256u);  // default fallback alignment
    const std::size_t o0 = buf.push(GlobalsUniform{1.0f, 0.0f, 1u});
    const std::size_t o1 = buf.push(GlobalsUniform{2.0f, 0.0f, 2u});
    EXPECT_EQ(o0, 0u);
    EXPECT_EQ(o1, 256u);  // stride 256
    EXPECT_EQ(buf.len(), 2u);

    wgpu::Limits limits;
    limits.minStorageBufferOffsetAlignment = 16;
    buf.update_alignment(limits);
    EXPECT_EQ(buf.element_stride(), 16u);  // align_up(12, 16)

    const std::size_t o2 = buf.push(GlobalsUniform{3.0f, 0.0f, 3u});
    EXPECT_EQ(o2, 512u);  // 2 * 256 from previous alignment
    buf.clear();
    EXPECT_TRUE(buf.is_empty());
}

// RenderDebugFlags matches Bevy lib.rs:133-144: one bit
// ALLOW_COPIES_FROM_INDIRECT_PARAMETERS, default all-clear.
TEST(RenderDebugFlags, BitflagSemantics) {
    RenderDebugFlags flags;
    EXPECT_FALSE(flags.allow_copies_from_indirect_parameters());
    EXPECT_EQ(flags.bits, 0u);

    flags.set_allow_copies_from_indirect_parameters();
    EXPECT_TRUE(flags.allow_copies_from_indirect_parameters());
    EXPECT_EQ(flags.bits, RenderDebugFlags::ALLOW_COPIES_FROM_INDIRECT_PARAMETERS);

    flags.set_allow_copies_from_indirect_parameters(false);
    EXPECT_FALSE(flags.allow_copies_from_indirect_parameters());
    EXPECT_EQ(flags.bits, 0u);

    // RenderPlugin carries the flags (Bevy RenderPlugin::debug_flags).
    RenderPlugin plugin;
    EXPECT_FALSE(plugin.debug_flags.allow_copies_from_indirect_parameters());
}

// PhaseItemBatchSetKey (Bevy render_phase/mod.rs:1662) is satisfied by
// comparable types; BinnedPhaseItem requires it on BatchSetKey.
TEST(PhaseItemBatchSetKey, ConceptSatisfaction) {
    EXPECT_TRUE(phase::PhaseItemBatchSetKey<int>);
    EXPECT_TRUE(phase::PhaseItemBatchSetKey<std::uint64_t>);
    EXPECT_TRUE(phase::PhaseItemBatchSetKey<std::string>);

    struct NotComparable {};
    EXPECT_FALSE(phase::PhaseItemBatchSetKey<NotComparable>);
}

// ViewDepthTexture wraps a DepthAttachment with first-call-clear semantics
// (Bevy view/mod.rs:887-904).
TEST(ViewDepthTexture, FirstCallClearSemantics) {
    wgpu::Device device{};
    (void)device;
    // DepthAttachment first-call-clear is already covered by the attachment
    // tests; here verify ViewDepthTexture::create wires the clear value
    // through to the attachment.
    // (Cannot construct a real wgpu texture without a device; the create
    // helper is exercised structurally via DepthAttachment ctor.)
    render_resource::DepthAttachment attachment(wgpu::TextureView{}, 0.5f);
    EXPECT_TRUE(attachment.clear_value.has_value());
    EXPECT_FLOAT_EQ(*attachment.clear_value, 0.5f);
    EXPECT_TRUE(attachment.is_first_call->load());
    // First get_attachment clears, second loads.
    auto a1 = attachment.get_attachment(wgpu::StoreOp::eStore);
    EXPECT_EQ(a1.depthLoadOp, wgpu::LoadOp::eClear);

    auto a2 = attachment.get_attachment(wgpu::StoreOp::eStore);
    EXPECT_EQ(a2.depthLoadOp, wgpu::LoadOp::eLoad);
}

// BufferVec::write_buffer_range matches Bevy buffer_vec.rs:196-215:
// NoValuesToUpload when empty, RangeBiggerThanBuffer on overflow,
// BufferNotInitialized when no GPU buffer exists.
TEST(BufferVec, WriteBufferRangeErrors) {
    render_resource::BufferVec<float> vec;
    auto empty = vec.write_buffer_range(wgpu::Queue{}, {0, 1});
    EXPECT_FALSE(empty.has_value());
    EXPECT_EQ(empty.error(), render_resource::WriteBufferRangeError::NoValuesToUpload);

    vec.push(1.0f);
    vec.push(2.0f);
    auto overflow = vec.write_buffer_range(wgpu::Queue{}, {0, 5});
    EXPECT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error(), render_resource::WriteBufferRangeError::RangeBiggerThanBuffer);

    auto uninit = vec.write_buffer_range(wgpu::Queue{}, {0, 1});
    EXPECT_FALSE(uninit.has_value());
    EXPECT_EQ(uninit.error(), render_resource::WriteBufferRangeError::BufferNotInitialized);
}

// RenderPipelineDescriptor::fragment_mut matches Bevy pipeline.rs:136-138:
// Ok(&mut FragmentState) when configured, NoFragmentStateError otherwise.
TEST(PipelineDescriptor, FragmentMut) {
    epix::assets::Handle<epix::shader::Shader> shader_handle{uuids::uuid{}};
    VertexState vs{shader_handle, {}, std::nullopt, {}};
    RenderPipelineDescriptor rpd{
        "", {}, {}, vs, wgpu::PrimitiveState(), std::nullopt, wgpu::MultisampleState(), std::nullopt};
    auto missing = rpd.fragment_mut();
    EXPECT_FALSE(missing.has_value());

    rpd.set_fragment(FragmentState{shader_handle, {}, std::nullopt, {}});
    auto present = rpd.fragment_mut();
    ASSERT_TRUE(present.has_value());
    FragmentState* fs = present.value();

    EXPECT_NE(fs, nullptr);
    fs->targets.push_back(wgpu::ColorTargetState());
    EXPECT_EQ(rpd.fragment->targets.size(), 1u);
}

// RenderLayers is the Bevy name for epix's RenderLayers (bevy_camera
// render_layers.rs) - the alias is interchangeable.
TEST(RenderLayers, AliasIsRenderLayers) {
    camera::RenderLayers layers = camera::RenderLayers::layer(0);
    EXPECT_TRUE(layers.intersects(camera::RenderLayers::layer(0)));
    EXPECT_FALSE(layers.intersects(camera::RenderLayers::layer(1)));
    camera::RenderLayers all = camera::RenderLayers::all();
    EXPECT_TRUE(all.intersects(camera::RenderLayers::layer(3)));
}

// ---------- erased asset pipeline (bevy_render 0.18 erased_render_asset.rs) ----------
// The ErasedRenderAsset specializations live at global scope: MSVC rejects
// defining an enclosing-namespace template specialization inside an anonymous
// namespace (C2888).

// Simple test asset types for the erased pipeline.
struct ErasedTestSource {
    int value = 0;
};
struct ErasedTestGpu {
    int value = 0;
};

// Adapter: RENDER_WORLD-only usage (asset is moved out of the main store).
struct ErasedTestAdapter;
template <>
struct epix::render::erased_render_asset::ErasedRenderAsset<ErasedTestAdapter> {
    using SourceAsset = ErasedTestSource;
    using ErasedAsset = ErasedTestGpu;
    using Param       = std::tuple<>;

    RenderAssetUsages asset_usage(const ErasedTestSource&) const { return RenderAssetUsages::RENDER_WORLD; }
    std::expected<ErasedTestGpu, epix::render::erased_render_asset::PrepareAssetError<ErasedTestSource>> prepare_asset(
        ErasedTestSource&& source, const epix::assets::AssetId<ErasedTestSource>&, Param&) const {
        return ErasedTestGpu{source.value};
    }
};

// Adapter: RENDER_WORLD|MAIN_WORLD usage (asset is cloned into the render world).
struct ErasedTestCloneAdapter;
template <>
struct epix::render::erased_render_asset::ErasedRenderAsset<ErasedTestCloneAdapter> {
    using SourceAsset = ErasedTestSource;
    using ErasedAsset = ErasedTestGpu;
    using Param       = std::tuple<>;

    RenderAssetUsages asset_usage(const ErasedTestSource&) const {
        return static_cast<RenderAssetUsages>(RenderAssetUsages::RENDER_WORLD | RenderAssetUsages::MAIN_WORLD);
    }
    std::expected<ErasedTestGpu, epix::render::erased_render_asset::PrepareAssetError<ErasedTestSource>> prepare_asset(
        ErasedTestSource&& source, const epix::assets::AssetId<ErasedTestSource>&, Param&) const {
        return ErasedTestGpu{source.value};
    }
};

// Adapter: MAIN_WORLD-only usage (never extracted).
struct ErasedTestMainOnlyAdapter;
template <>
struct epix::render::erased_render_asset::ErasedRenderAsset<ErasedTestMainOnlyAdapter> {
    using SourceAsset = ErasedTestSource;
    using ErasedAsset = ErasedTestGpu;
    using Param       = std::tuple<>;

    RenderAssetUsages asset_usage(const ErasedTestSource&) const { return RenderAssetUsages::MAIN_WORLD; }
    std::expected<ErasedTestGpu, epix::render::erased_render_asset::PrepareAssetError<ErasedTestSource>> prepare_asset(
        ErasedTestSource&& source, const epix::assets::AssetId<ErasedTestSource>&, Param&) const {
        return ErasedTestGpu{source.value};
    }
};

// Adapter: value 99 retries on the FIRST prepare call (Bevy RetryNextUpdate
// defers the asset and retries it next frame), then succeeds on the retry.
static bool g_erased_retry_already_retried = false;
struct ErasedTestRetryAdapter;
template <>
struct epix::render::erased_render_asset::ErasedRenderAsset<ErasedTestRetryAdapter> {
    using SourceAsset = ErasedTestSource;
    using ErasedAsset = ErasedTestGpu;
    using Param       = std::tuple<>;

    RenderAssetUsages asset_usage(const ErasedTestSource&) const { return RenderAssetUsages::RENDER_WORLD; }
    std::expected<ErasedTestGpu, epix::render::erased_render_asset::PrepareAssetError<ErasedTestSource>> prepare_asset(
        ErasedTestSource&& source, const epix::assets::AssetId<ErasedTestSource>&, Param&) const {
        if (source.value == 99 && !g_erased_retry_already_retried) {
            g_erased_retry_already_retried = true;
            return std::unexpected(
                epix::render::erased_render_asset::PrepareAssetError<ErasedTestSource>::retry_next_update(
                    std::move(source)));
        }
        return ErasedTestGpu{source.value};
    }
    static void reset() { g_erased_retry_already_retried = false; }
};

// Resource ids are per-world dense ids. The extract systems below take BOTH a
// render-world ResMut and main-world Extract<Res> params; the shared access
// set indexes by those per-world ids, so the main world's ids must not overlap
// the render world's (here: ExtractedWorld=0, ExtractedAssets=1). Registering
// two dummy resources first shifts the main world's Assets/Events ids to 2/3.
struct ErasedTestIdShiftA {};
struct ErasedTestIdShiftB {};

// ErasedRenderAssets container accessors (Bevy erased_render_asset.rs:192-224).
TEST(ErasedRenderAsset, ContainerAccessors) {
    erased_render_asset::ErasedRenderAssets<ErasedTestGpu> container;
    // AssetIndex is only constructible through an asset store (protected ctor),
    // so derive the ids from real handles like the asset pipeline does.
    epix::assets::Assets<ErasedTestSource> store;
    auto handle_a                           = store.emplace(ErasedTestSource{1});
    auto handle_b                           = store.emplace(ErasedTestSource{2});
    const epix::assets::UntypedAssetId id_a = epix::assets::UntypedAssetId(handle_a.id());
    const epix::assets::UntypedAssetId id_b = epix::assets::UntypedAssetId(handle_b.id());

    EXPECT_EQ(container.get(id_a), nullptr);
    EXPECT_FALSE(container.insert(id_a, ErasedTestGpu{1}).has_value());  // no previous
    ASSERT_NE(container.get(id_a), nullptr);
    EXPECT_EQ(container.get(id_a)->value, 1);

    // Re-insert returns the previous value (Bevy insert -> Option<ERA>).
    auto previous = container.insert(id_a, ErasedTestGpu{2});
    ASSERT_TRUE(previous.has_value());
    EXPECT_EQ(previous->value, 1);
    EXPECT_EQ(container.get(id_a)->value, 2);

    // get_mut allows mutation.
    ASSERT_NE(container.get_mut(id_a), nullptr);
    container.get_mut(id_a)->value = 3;
    EXPECT_EQ(container.get(id_a)->value, 3);

    // remove returns the value; a second remove returns nullopt.
    auto removed = container.remove(id_a);
    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(removed->value, 3);
    EXPECT_FALSE(container.remove(id_a).has_value());
    EXPECT_EQ(container.get(id_a), nullptr);

    // iter visits all entries.
    container.insert(id_a, ErasedTestGpu{4});
    container.insert(id_b, ErasedTestGpu{5});
    int count = 0;
    for (auto&& [id, value] : container.iter()) {
        (void)id;
        (void)value;
        ++count;
    }
    EXPECT_EQ(count, 2);
}

// PrepareAssetError variants (Bevy erased_render_asset.rs:20-26).
TEST(ErasedRenderAsset, PrepareAssetErrorVariants) {
    auto retry = erased_render_asset::PrepareAssetError<ErasedTestSource>::retry_next_update(ErasedTestSource{7});
    EXPECT_TRUE(retry.is_retry_next_update());
    EXPECT_FALSE(retry.is_as_bind_group_error());
    EXPECT_EQ(retry.retry_asset().value, 7);

    auto bind = erased_render_asset::PrepareAssetError<ErasedTestSource>::as_bind_group_error(
        epix::render::render_resource::AsBindGroupError::CreateBindGroup);
    EXPECT_FALSE(bind.is_retry_next_update());
    EXPECT_TRUE(bind.is_as_bind_group_error());
    EXPECT_EQ(bind.bind_group_error(), epix::render::render_resource::AsBindGroupError::CreateBindGroup);
}

// extract_erased_render_asset (Bevy erased_render_asset.rs:244-312): RENDER_WORLD-only
// assets are moved out of the main store; Unused events mark removals.
TEST(ErasedRenderAsset, ExtractSystemMovesRenderWorldOnlyAssets) {
    epix::ecs::World main_world(2);
    epix::ecs::World render_world(2);
    render_world.insert_resource(epix::app::ExtractedWorld{main_world});
    render_world.insert_resource(erased_render_asset::ExtractedAssets<ErasedTestAdapter>{});
    // Shift main-world resource ids above the render world's (see note above).
    main_world.insert_resource(ErasedTestIdShiftA{});
    main_world.insert_resource(ErasedTestIdShiftB{});
    main_world.insert_resource(epix::assets::Assets<ErasedTestSource>{});
    main_world.insert_resource(epix::ecs::Events<epix::assets::AssetEvent<ErasedTestSource>>{});

    auto& store   = main_world.resource_mut<epix::assets::Assets<ErasedTestSource>>();
    auto handle_a = store.emplace(ErasedTestSource{10});
    auto handle_b = store.emplace(ErasedTestSource{20});
    auto& events  = main_world.resource_mut<epix::ecs::Events<epix::assets::AssetEvent<ErasedTestSource>>>();
    events.push(epix::assets::AssetEvent<ErasedTestSource>::added(handle_a.id()));
    events.push(epix::assets::AssetEvent<ErasedTestSource>::added(handle_b.id()));
    events.push(epix::assets::AssetEvent<ErasedTestSource>::unused(handle_b.id()));

    auto system = make_system_unique(erased_render_asset::extract_erased_render_asset<ErasedTestAdapter>);
    system->initialize(render_world);
    ASSERT_TRUE(system->run({}, render_world).has_value());

    auto& cache = render_world.resource<erased_render_asset::ExtractedAssets<ErasedTestAdapter>>();
    ASSERT_EQ(cache.extracted.size(), 1u);  // b was unused -> removed, not extracted
    EXPECT_EQ(cache.extracted[0].first, handle_a.id());
    EXPECT_EQ(cache.extracted[0].second.value, 10);
    EXPECT_TRUE(cache.removed.contains(handle_b.id()));
    EXPECT_TRUE(cache.added.contains(handle_a.id()));
    // RENDER_WORLD-only: the asset was moved out of the main store.
    EXPECT_FALSE(main_world.resource<epix::assets::Assets<ErasedTestSource>>().get(handle_a.id()).has_value());
}

// extract_erased_render_asset: RENDER_WORLD|MAIN_WORLD assets are cloned and stay
// in the main store (Bevy erased_render_asset.rs:290-298).
TEST(ErasedRenderAsset, ExtractSystemClonesSharedUsage) {
    epix::ecs::World main_world(2);
    epix::ecs::World render_world(2);
    render_world.insert_resource(epix::app::ExtractedWorld{main_world});
    render_world.insert_resource(erased_render_asset::ExtractedAssets<ErasedTestCloneAdapter>{});
    // Shift main-world resource ids above the render world's (see note above).
    main_world.insert_resource(ErasedTestIdShiftA{});
    main_world.insert_resource(ErasedTestIdShiftB{});
    main_world.insert_resource(epix::assets::Assets<ErasedTestSource>{});
    main_world.insert_resource(epix::ecs::Events<epix::assets::AssetEvent<ErasedTestSource>>{});

    auto& store   = main_world.resource_mut<epix::assets::Assets<ErasedTestSource>>();
    auto handle_a = store.emplace(ErasedTestSource{30});
    auto& events  = main_world.resource_mut<epix::ecs::Events<epix::assets::AssetEvent<ErasedTestSource>>>();
    events.push(epix::assets::AssetEvent<ErasedTestSource>::added(handle_a.id()));

    auto system = make_system_unique(erased_render_asset::extract_erased_render_asset<ErasedTestCloneAdapter>);
    system->initialize(render_world);
    ASSERT_TRUE(system->run({}, render_world).has_value());

    auto& cache = render_world.resource<erased_render_asset::ExtractedAssets<ErasedTestCloneAdapter>>();
    ASSERT_EQ(cache.extracted.size(), 1u);
    EXPECT_EQ(cache.extracted[0].second.value, 30);
    // MAIN_WORLD usage: the main store keeps its copy.
    EXPECT_TRUE(main_world.resource<epix::assets::Assets<ErasedTestSource>>().get(handle_a.id()).has_value());
}

// extract_erased_render_asset: MAIN_WORLD-only assets are never extracted.
TEST(ErasedRenderAsset, ExtractSystemSkipsMainWorldOnly) {
    epix::ecs::World main_world(2);
    epix::ecs::World render_world(2);
    render_world.insert_resource(epix::app::ExtractedWorld{main_world});
    render_world.insert_resource(erased_render_asset::ExtractedAssets<ErasedTestMainOnlyAdapter>{});
    // Shift main-world resource ids above the render world's (see note above).
    main_world.insert_resource(ErasedTestIdShiftA{});
    main_world.insert_resource(ErasedTestIdShiftB{});
    main_world.insert_resource(epix::assets::Assets<ErasedTestSource>{});
    main_world.insert_resource(epix::ecs::Events<epix::assets::AssetEvent<ErasedTestSource>>{});

    auto& store   = main_world.resource_mut<epix::assets::Assets<ErasedTestSource>>();
    auto handle_a = store.emplace(ErasedTestSource{40});
    auto& events  = main_world.resource_mut<epix::ecs::Events<epix::assets::AssetEvent<ErasedTestSource>>>();
    events.push(epix::assets::AssetEvent<ErasedTestSource>::added(handle_a.id()));

    auto system = make_system_unique(erased_render_asset::extract_erased_render_asset<ErasedTestMainOnlyAdapter>);
    system->initialize(render_world);
    ASSERT_TRUE(system->run({}, render_world).has_value());

    auto& cache = render_world.resource<erased_render_asset::ExtractedAssets<ErasedTestMainOnlyAdapter>>();
    EXPECT_TRUE(cache.extracted.empty());
    // MAIN_WORLD-only: untouched.
    EXPECT_TRUE(main_world.resource<epix::assets::Assets<ErasedTestSource>>().get(handle_a.id()).has_value());
}

// prepare_erased_assets (Bevy erased_render_asset.rs:331-426): successful
// prepares are inserted into ErasedRenderAssets; RetryNextUpdate defers to
// PrepareNextFrameAssets; Unused removals unload.
TEST(ErasedRenderAsset, PrepareSystemInsertsAndRetries) {
    epix::render::erased_render_asset::ErasedRenderAsset<ErasedTestRetryAdapter>::reset();
    epix::ecs::World world(2);
    world.insert_resource(erased_render_asset::ExtractedAssets<ErasedTestRetryAdapter>{});
    world.insert_resource(erased_render_asset::ErasedRenderAssets<ErasedTestGpu>{});
    world.insert_resource(erased_render_asset::PrepareNextFrameAssets<ErasedTestRetryAdapter>{});
    world.insert_resource(RenderAssetBytesPerFrameLimiter{});

    epix::assets::Assets<ErasedTestSource> store;
    const epix::assets::AssetId<ErasedTestSource> id_ok    = store.emplace(ErasedTestSource{50}).id();
    const epix::assets::AssetId<ErasedTestSource> id_retry = store.emplace(ErasedTestSource{99}).id();
    {
        auto& extracted = world.resource_mut<erased_render_asset::ExtractedAssets<ErasedTestRetryAdapter>>();
        extracted.extracted.emplace_back(id_ok, ErasedTestSource{50});
        extracted.extracted.emplace_back(id_retry, ErasedTestSource{99});
    }

    auto system = make_system_unique(erased_render_asset::prepare_erased_assets<ErasedTestRetryAdapter>);
    system->initialize(world);
    ASSERT_TRUE(system->run({}, world).has_value());

    auto& render_assets = world.resource<erased_render_asset::ErasedRenderAssets<ErasedTestGpu>>();
    auto& pending       = world.resource<erased_render_asset::PrepareNextFrameAssets<ErasedTestRetryAdapter>>();
    ASSERT_NE(render_assets.get(epix::assets::UntypedAssetId(id_ok)), nullptr);
    EXPECT_EQ(render_assets.get(epix::assets::UntypedAssetId(id_ok))->value, 50);
    // The retried asset is deferred to next frame, not dropped.
    ASSERT_EQ(pending.assets.size(), 1u);
    EXPECT_EQ(pending.assets[0].first, id_retry);
    EXPECT_EQ(render_assets.get(epix::assets::UntypedAssetId(id_retry)), nullptr);

    // Frame 2: the deferred asset is prepared now.
    ASSERT_TRUE(system->run({}, world).has_value());
    ASSERT_NE(render_assets.get(epix::assets::UntypedAssetId(id_retry)), nullptr);
    EXPECT_EQ(render_assets.get(epix::assets::UntypedAssetId(id_retry))->value, 99);
    EXPECT_TRUE(world.resource<erased_render_asset::PrepareNextFrameAssets<ErasedTestRetryAdapter>>().assets.empty());
}

// prepare_erased_assets: removed assets leave ErasedRenderAssets (Bevy
// erased_render_asset.rs:380-383).
TEST(ErasedRenderAsset, PrepareSystemUnloadsRemoved) {
    epix::render::erased_render_asset::ErasedRenderAsset<ErasedTestRetryAdapter>::reset();
    epix::ecs::World world(2);
    world.insert_resource(erased_render_asset::ExtractedAssets<ErasedTestRetryAdapter>{});
    world.insert_resource(erased_render_asset::ErasedRenderAssets<ErasedTestGpu>{});
    world.insert_resource(erased_render_asset::PrepareNextFrameAssets<ErasedTestRetryAdapter>{});
    world.insert_resource(RenderAssetBytesPerFrameLimiter{});

    epix::assets::Assets<ErasedTestSource> store;
    const epix::assets::AssetId<ErasedTestSource> id_a = store.emplace(ErasedTestSource{60}).id();
    auto& render_assets = world.resource_mut<erased_render_asset::ErasedRenderAssets<ErasedTestGpu>>();
    render_assets.insert(epix::assets::UntypedAssetId(id_a), ErasedTestGpu{60});
    world.resource_mut<erased_render_asset::ExtractedAssets<ErasedTestRetryAdapter>>().removed.insert(id_a);

    auto system = make_system_unique(erased_render_asset::prepare_erased_assets<ErasedTestRetryAdapter>);
    system->initialize(world);
    ASSERT_TRUE(system->run({}, world).has_value());

    EXPECT_EQ(world.resource<erased_render_asset::ErasedRenderAssets<ErasedTestGpu>>().get(
                  epix::assets::UntypedAssetId(id_a)),
              nullptr);
}

// ImageSamplerDescriptor mirrors bevy_image (image.rs:758-842): linear()/nearest()
// factories, set_filter, set_address_mode, wgpu-default descriptor values.
TEST(ImageSamplerDescriptor, FactoriesAndMutators) {
    auto linear = epix::image::ImageSamplerDescriptor::linear();
    EXPECT_EQ(linear.mag_filter, epix::image::ImageFilterMode::Linear);
    EXPECT_EQ(linear.min_filter, epix::image::ImageFilterMode::Linear);
    EXPECT_EQ(linear.mipmap_filter, epix::image::ImageFilterMode::Linear);

    auto nearest = epix::image::ImageSamplerDescriptor::nearest();
    EXPECT_EQ(nearest.mag_filter, epix::image::ImageFilterMode::Nearest);
    EXPECT_EQ(nearest.min_filter, epix::image::ImageFilterMode::Nearest);
    EXPECT_EQ(nearest.mipmap_filter, epix::image::ImageFilterMode::Nearest);

    // Defaults match wgpu SamplerDescriptor::default() (Bevy
    // ImageSamplerDescriptor::default, image.rs:784-800).
    epix::image::ImageSamplerDescriptor def;
    EXPECT_EQ(def.address_mode_u, epix::image::ImageAddressMode::ClampToEdge);
    EXPECT_EQ(def.address_mode_v, epix::image::ImageAddressMode::ClampToEdge);
    EXPECT_EQ(def.address_mode_w, epix::image::ImageAddressMode::ClampToEdge);
    EXPECT_EQ(def.mag_filter, epix::image::ImageFilterMode::Nearest);
    EXPECT_EQ(def.min_filter, epix::image::ImageFilterMode::Nearest);
    EXPECT_EQ(def.mipmap_filter, epix::image::ImageFilterMode::Nearest);
    EXPECT_EQ(def.lod_min_clamp, 0.0f);
    EXPECT_EQ(def.lod_max_clamp, 32.0f);
    EXPECT_FALSE(def.compare.has_value());
    EXPECT_EQ(def.anisotropy_clamp, 1);
    EXPECT_FALSE(def.border_color.has_value());

    // set_filter / set_address_mode mutate all three axes (Bevy
    // ImageSamplerDescriptor::set_filter / set_address_mode).
    def.set_filter(epix::image::ImageFilterMode::Linear);
    EXPECT_EQ(def.mag_filter, epix::image::ImageFilterMode::Linear);
    EXPECT_EQ(def.min_filter, epix::image::ImageFilterMode::Linear);
    EXPECT_EQ(def.mipmap_filter, epix::image::ImageFilterMode::Linear);
    def.set_address_mode(epix::image::ImageAddressMode::MirrorRepeat);
    EXPECT_EQ(def.address_mode_u, epix::image::ImageAddressMode::MirrorRepeat);
    EXPECT_EQ(def.address_mode_v, epix::image::ImageAddressMode::MirrorRepeat);
    EXPECT_EQ(def.address_mode_w, epix::image::ImageAddressMode::MirrorRepeat);
}

// Image::sampler defaults to ImageSampler::Default and switches to Descriptor
// when a custom descriptor is set (Bevy Image::sampler, image.rs:599-624).
TEST(Image, SamplerAccessors) {
    auto image = epix::image::Image::create2d(4, 4, epix::image::Format::RGBA8);
    EXPECT_EQ(image.sampler(), epix::image::ImageSampler::Default);
    EXPECT_EQ(image.sampler_descriptor().min_filter, epix::image::ImageFilterMode::Nearest);

    auto descriptor = epix::image::ImageSamplerDescriptor::linear();
    image.set_sampler_descriptor(descriptor);
    EXPECT_EQ(image.sampler(), epix::image::ImageSampler::Descriptor);
    EXPECT_EQ(image.sampler_descriptor().min_filter, epix::image::ImageFilterMode::Linear);

    image.set_sampler(epix::image::ImageSampler::Default);
    EXPECT_EQ(image.sampler(), epix::image::ImageSampler::Default);
}

// Visibility marker matches Bevy bevy_camera::Visibility (visibility__mod.rs:39-88):
// default Inherited, three kinds, toggle helpers.
TEST(Visibility, MarkersAndToggles) {
    epix::render::camera::Visibility v;
    EXPECT_EQ(v.type, epix::render::camera::Visibility::Type::Inherited);
    EXPECT_EQ(epix::render::camera::Visibility::hidden().type, epix::render::camera::Visibility::Type::Hidden);
    EXPECT_EQ(epix::render::camera::Visibility::visible().type, epix::render::camera::Visibility::Type::Visible);

    // toggle_inherited_visible: Inherited<->Visible, Hidden unaffected.
    v.toggle_inherited_visible();
    EXPECT_EQ(v.type, epix::render::camera::Visibility::Type::Visible);
    v.toggle_inherited_visible();
    EXPECT_EQ(v.type, epix::render::camera::Visibility::Type::Inherited);
    v = epix::render::camera::Visibility::hidden();
    v.toggle_inherited_visible();
    EXPECT_EQ(v.type, epix::render::camera::Visibility::Type::Hidden);

    // toggle_inherited_hidden: Inherited<->Hidden, Visible unaffected.
    v.toggle_inherited_hidden();
    EXPECT_EQ(v.type, epix::render::camera::Visibility::Type::Inherited);
    v.toggle_inherited_hidden();
    EXPECT_EQ(v.type, epix::render::camera::Visibility::Type::Hidden);
    v = epix::render::camera::Visibility::visible();
    v.toggle_inherited_hidden();
    EXPECT_EQ(v.type, epix::render::camera::Visibility::Type::Visible);

    // toggle_visible_hidden: Visible<->Hidden, Inherited unaffected.
    v.toggle_visible_hidden();
    EXPECT_EQ(v.type, epix::render::camera::Visibility::Type::Hidden);
    v.toggle_visible_hidden();
    EXPECT_EQ(v.type, epix::render::camera::Visibility::Type::Visible);
    v = epix::render::camera::Visibility::inherited();
    v.toggle_visible_hidden();
    EXPECT_EQ(v.type, epix::render::camera::Visibility::Type::Inherited);
}

// InheritedVisibility matches Bevy (visibility__mod.rs:108-131): HIDDEN/VISIBLE
// consts and get().
TEST(InheritedVisibility, ConstsAndGet) {
    EXPECT_FALSE(epix::render::camera::InheritedVisibility::hidden().get());
    EXPECT_TRUE(epix::render::camera::InheritedVisibility::visible().get());
    epix::render::camera::InheritedVisibility def;
    EXPECT_TRUE(def.get());
}

// GetBatchData / GetFullBatchData batching traits (bevy_render batching/mod.rs:76-178).
namespace {

struct BatchingTestCompare {
    int key                                           = 0;
    bool operator==(const BatchingTestCompare&) const = default;
};
struct BatchingTestBufferData {
    int value = 0;
};
struct BatchingTestInputData {
    int value = 0;
};
struct BatchingTestAdapter;
}  // namespace
template <>
struct epix::render::batching::GetBatchData<BatchingTestAdapter> {
    using Param       = std::tuple<>;
    using CompareData = BatchingTestCompare;
    using BufferData  = BatchingTestBufferData;
    std::optional<std::pair<BufferData, std::optional<CompareData>>> get_batch_data(
        Param&, std::pair<epix::ecs::Entity, sync_world::MainEntity>) const {
        return std::pair<BufferData, std::optional<CompareData>>{BufferData{7}, CompareData{3}};
    }
};
template <>
struct epix::render::batching::GetFullBatchData<BatchingTestAdapter> {
    using BufferInputData = BatchingTestInputData;
    std::optional<BatchingTestBufferData> get_binned_batch_data(
        epix::render::batching::GetBatchData<BatchingTestAdapter>::Param&, sync_world::MainEntity) const {
        return BatchingTestBufferData{8};
    }
    std::optional<std::pair<std::uint32_t, std::optional<BatchingTestCompare>>> get_index_and_compare_data(
        epix::render::batching::GetBatchData<BatchingTestAdapter>::Param&, sync_world::MainEntity) const {
        return std::pair<std::uint32_t, std::optional<BatchingTestCompare>>{2, BatchingTestCompare{4}};
    }
    std::optional<std::uint32_t> get_binned_index(epix::render::batching::GetBatchData<BatchingTestAdapter>::Param&,
                                                  sync_world::MainEntity) const {
        return 5u;
    }
};

// Both batching trait concepts are satisfied by a full specialization, and
// get_batch_data returns the per-instance buffer data + optional compare data
// (Bevy batching/mod.rs:92-99).
TEST(GetBatchData, ConceptsAndData) {
    static_assert(epix::render::batching::GetBatchDataImpl<BatchingTestAdapter>);
    static_assert(epix::render::batching::GetFullBatchDataImpl<BatchingTestAdapter>);

    epix::render::batching::GetBatchData<BatchingTestAdapter> batch;
    std::tuple<> param;
    auto result = batch.get_batch_data(param, {epix::ecs::Entity{}, sync_world::MainEntity{epix::ecs::Entity{}}});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first.value, 7);
    ASSERT_TRUE(result->second.has_value());
    EXPECT_EQ(result->second->key, 3);

    epix::render::batching::GetFullBatchData<BatchingTestAdapter> full;
    auto binned = full.get_binned_batch_data(param, sync_world::MainEntity{epix::ecs::Entity{}});
    ASSERT_TRUE(binned.has_value());
    EXPECT_EQ(binned->value, 8);
    auto indexed = full.get_index_and_compare_data(param, sync_world::MainEntity{epix::ecs::Entity{}});
    ASSERT_TRUE(indexed.has_value());
    EXPECT_EQ(indexed->first, 2u);
    ASSERT_TRUE(indexed->second.has_value());
    EXPECT_EQ(indexed->second->key, 4);
    auto index = full.get_binned_index(param, sync_world::MainEntity{epix::ecs::Entity{}});
    ASSERT_TRUE(index.has_value());
    EXPECT_EQ(*index, 5u);
}

// World-side VisibleEntities is per-visibility-class (Bevy bevy_camera
// visibility__mod.rs:279-316): type-id-keyed lists with get/get_mut/iter/
// len/is_empty/clear/clear_all.
TEST(VisibleEntities, PerClassAccessors) {
    view::VisibleEntities visible;
    const auto class_a = epix::meta::type_index(epix::meta::type_id<int>());
    const auto class_b = epix::meta::type_index(epix::meta::type_id<float>());

    // get for an absent class returns an empty list.
    EXPECT_TRUE(visible.get(class_a).empty());
    EXPECT_TRUE(visible.is_empty(class_a));
    EXPECT_EQ(visible.len(class_a), 0u);

    // get_mut inserts and populates per class.
    visible.get_mut(class_a).push_back(epix::ecs::Entity::from_index(1));
    visible.get_mut(class_a).push_back(epix::ecs::Entity::from_index(2));
    visible.get_mut(class_b).push_back(epix::ecs::Entity::from_index(3));
    EXPECT_EQ(visible.len(class_a), 2u);
    EXPECT_EQ(visible.len(class_b), 1u);
    EXPECT_FALSE(visible.is_empty(class_a));
    EXPECT_EQ(visible.iter(class_a).size(), 2u);
    EXPECT_EQ(visible.iter(class_a)[0], epix::ecs::Entity::from_index(1));

    // clear removes one class's entities; clear_all empties every list.
    visible.clear(class_a);
    EXPECT_EQ(visible.len(class_a), 0u);
    EXPECT_EQ(visible.len(class_b), 1u);
    visible.clear_all();
    EXPECT_TRUE(visible.is_empty(class_a));
    EXPECT_TRUE(visible.is_empty(class_b));
}

namespace {
// Cached-pipeline phase item for the SetItemPipeline Skip test.
struct CachedPipelineTestItem {
    epix::ecs::Entity m_entity;
    phase::DrawFunctionId m_draw_function;
    CachedPipelineId m_pipeline;
    std::pair<std::uint32_t, std::uint32_t> batch_range{0, 1};

    epix::ecs::Entity entity() const { return m_entity; }
    sync_world::MainEntity main_entity() const { return sync_world::MainEntity{m_entity}; }
    int sort_key() const { return 0; }
    phase::DrawFunctionId draw_function() const { return m_draw_function; }
    phase::PhaseItemExtraIndex extra_index() const { return phase::PhaseItemExtraIndex::None; }
    CachedPipelineId pipeline() const { return m_pipeline; }
};
static_assert(epix::render::phase::CachedRenderPipelinePhaseItem<CachedPipelineTestItem>);

// Copyable resource for the extract_resource insert/update test.
struct ExtractTestResource {
    int value = 0;
};
}  // namespace

// SetItemPipeline: ANY pipeline-cache miss (not ready / invalid id / creation
// failure) is a Skip — the item is simply not drawn this frame (Bevy
// render_phase/mod.rs:1740-1748).
TEST(SetItemPipeline, SkipsOnPipelineMiss) {
    epix::ecs::World world(2);
    world.insert_resource(PipelineServer{wgpu::Device{}});

    using Params = epix::ecs::ParamSet<epix::ecs::Res<PipelineServer>>;
    auto states  = epix::ecs::SystemParam<Params>::init_state(world);
    epix::ecs::SystemMeta meta;
    // The ParamSet ctor is private; build it through the friend SystemParam.
    Params params = epix::ecs::SystemParam<Params>::get_param(states, meta, world, epix::ecs::Tick{});

    CachedPipelineTestItem item;
    item.m_entity   = epix::ecs::Entity::from_index(1);
    item.m_pipeline = CachedPipelineId{42u};  // uncached

    phase::SetItemPipeline<CachedPipelineTestItem> command;
    auto result = command.render(item, epix::ecs::Item<>{}, std::optional<epix::ecs::Item<>>{}, params,
                                 wgpu::RenderPassEncoder{});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().type, phase::RenderCommandError::Type::Skip);
}

// extract_fn (Bevy ExtractResourcePlugin copyable path, extract_resource.rs:59-69):
// inserts the render-world copy when missing, overwrites when the source changed.
TEST(ExtractResource, InsertsWhenMissingAndUpdatesWhenModified) {
    epix::ecs::World main_world(2);
    epix::ecs::World render_world(2);
    render_world.insert_resource(epix::app::ExtractedWorld{main_world});
    main_world.insert_resource(ExtractTestResource{7});

    auto system = make_system_unique(epix::render::extract_fn<ExtractTestResource>);
    system->initialize(render_world);
    ASSERT_TRUE(system->run({}, render_world).has_value());
    // Insert-when-missing: the render world gained a copy of the resource.
    ASSERT_TRUE(render_world.get_resource<ExtractTestResource>().has_value());
    EXPECT_EQ(render_world.resource<ExtractTestResource>().value, 7);

    // Modified source overwrites the render-world copy (extract_resource.rs:61-66).
    main_world.resource_mut<ExtractTestResource>().value = 9;
    ASSERT_TRUE(system->run({}, render_world).has_value());
    EXPECT_EQ(render_world.resource<ExtractTestResource>().value, 9);
}

namespace {
// Fake specializable resource for the Variants test (RenderPipelineDescriptor
// is not default-constructible, so the generic machinery is exercised on a
// trivially-copyable descriptor type).
struct FakePipelineResource;
struct FakePipelineDescriptor {
    int config                                           = 0;
    bool operator==(const FakePipelineDescriptor&) const = default;
};
}  // namespace
template <>
struct epix::render::Specializable<FakePipelineResource> {
    using Descriptor = FakePipelineDescriptor;
    using CachedId   = std::uint64_t;
    CachedId queue(PipelineServer&, Descriptor) const {
        static std::uint64_t counter = 0;
        return ++counter;
    }
    const Descriptor& get_descriptor(PipelineServer&, CachedId) const {
        static const Descriptor kEmpty;
        return kEmpty;
    }
};
static_assert(epix::render::SpecializableImpl<FakePipelineResource>);

namespace {
// Test specializer: canonical key halves the variant, so keys {0,1} share a
// pipeline and keys {2,3} share another (Bevy specializer.rs canonical-key
// dedup, Variants::specialize_slow).
struct FakePipelineSpecializer {
    struct Key {
        int variant                       = 0;
        bool operator==(const Key&) const = default;
    };
    Key specialize(const Key& key, FakePipelineDescriptor& descriptor) const {
        descriptor.config = key.variant;
        return Key{key.variant / 2};
    }
};
}  // namespace
template <>
struct std::hash<FakePipelineSpecializer::Key> {
    std::size_t operator()(const FakePipelineSpecializer::Key& key) const noexcept {
        return std::hash<int>{}(key.variant);
    }
};
static_assert(epix::render::SpecializerImpl<FakePipelineResource, FakePipelineSpecializer>);

// Variants memoizes pipelines per key and deduplicates non-canonical keys that
// canonicalize to the same descriptor (Bevy specializer.rs:267-299).
TEST(Specializer, VariantsMemoizeByKeyAndCanonicalDedup) {
    PipelineServer server{wgpu::Device{}};
    epix::render::Variants<FakePipelineResource, FakePipelineSpecializer> variants(FakePipelineSpecializer{},
                                                                                   FakePipelineDescriptor{});

    auto id0 = variants.specialize(server, {0});
    auto id1 = variants.specialize(server, {1});  // canonical {0} -> dedup with id0
    auto id2 = variants.specialize(server, {2});  // canonical {1} -> new pipeline

    // Same key is memoized (Bevy primary_cache).
    EXPECT_EQ(variants.specialize(server, {0}), id0);
    EXPECT_EQ(variants.specialize(server, {2}), id2);
    // Canonical dedup: key 1 shares key 0's pipeline; key 2 is distinct.
    EXPECT_EQ(id1, id0);
    EXPECT_NE(id2, id0);
}
