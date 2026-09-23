#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <epix/time.hpp>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

using namespace epix::ecs;
using namespace epix::render;

namespace {

/** @brief Spawn a render-world 3D view with the requested phase membership. */
struct Core3dTestView {
    Entity entity;
    view::RetainedViewEntity retained;
};

}  // namespace

// Bevy core_3d::graph exposes all thirty labels in this exact order. Enum
// values are part of the public contract because plugins extend the graph by
// label, so the ordinal positions are asserted directly.
TEST(Node3d, ExposesEveryBevyLabelInOrder) {
    using Node = epix::core_graph::core_3d::Node3d;
    EXPECT_EQ(std::to_underlying(Node::MsaaWriteback), 0u);
    EXPECT_EQ(std::to_underlying(Node::EarlyPrepass), 1u);
    EXPECT_EQ(std::to_underlying(Node::EarlyDownsampleDepth), 2u);
    EXPECT_EQ(std::to_underlying(Node::LatePrepass), 3u);
    EXPECT_EQ(std::to_underlying(Node::EarlyDeferredPrepass), 4u);
    EXPECT_EQ(std::to_underlying(Node::LateDeferredPrepass), 5u);
    EXPECT_EQ(std::to_underlying(Node::CopyDeferredLightingId), 6u);
    EXPECT_EQ(std::to_underlying(Node::EndPrepasses), 7u);
    EXPECT_EQ(std::to_underlying(Node::StartMainPass), 8u);
    EXPECT_EQ(std::to_underlying(Node::MainOpaquePass), 9u);
    EXPECT_EQ(std::to_underlying(Node::MainTransmissivePass), 10u);
    EXPECT_EQ(std::to_underlying(Node::MainTransparentPass), 11u);
    EXPECT_EQ(std::to_underlying(Node::EndMainPass), 12u);
    EXPECT_EQ(std::to_underlying(Node::Wireframe), 13u);
    EXPECT_EQ(std::to_underlying(Node::StartMainPassPostProcessing), 14u);
    EXPECT_EQ(std::to_underlying(Node::LateDownsampleDepth), 15u);
    EXPECT_EQ(std::to_underlying(Node::MotionBlur), 16u);
    EXPECT_EQ(std::to_underlying(Node::Taa), 17u);
    EXPECT_EQ(std::to_underlying(Node::DlssSuperResolution), 18u);
    EXPECT_EQ(std::to_underlying(Node::DlssRayReconstruction), 19u);
    EXPECT_EQ(std::to_underlying(Node::Bloom), 20u);
    EXPECT_EQ(std::to_underlying(Node::AutoExposure), 21u);
    EXPECT_EQ(std::to_underlying(Node::DepthOfField), 22u);
    EXPECT_EQ(std::to_underlying(Node::PostProcessing), 23u);
    EXPECT_EQ(std::to_underlying(Node::Tonemapping), 24u);
    EXPECT_EQ(std::to_underlying(Node::Fxaa), 25u);
    EXPECT_EQ(std::to_underlying(Node::Smaa), 26u);
    EXPECT_EQ(std::to_underlying(Node::Upscaling), 27u);
    EXPECT_EQ(std::to_underlying(Node::ContrastAdaptiveSharpening), 28u);
    EXPECT_EQ(std::to_underlying(Node::EndMainPassPostProcessing), 29u);
    static_assert(epix::core_graph::core_3d::CORE_3D_DEPTH_FORMAT == wgpu::TextureFormat::eDepth32Float);
}

// Bevy Core3dPlugin installs the prepass/deferred labels, both main-pass
// endpoints, tonemapping, and upscaling, and wires them as one chain. Optional
// post-processing plugins own their own labels later.
TEST(Core3dGraph, MatchesBevyDefaultNodesAndChain) {
    epix::ecs::World world(1);
    epix::render::graph::RenderGraph root_graph;
    epix::core_graph::core_3d::Core3d.add_to(root_graph, world);
    const auto core3d = root_graph.get_sub_graph(epix::core_graph::core_3d::Core3d);
    ASSERT_TRUE(core3d.has_value());

    using Node = epix::core_graph::core_3d::Node3d;
    for (const auto node :
         {Node::EarlyPrepass, Node::LatePrepass, Node::EarlyDeferredPrepass, Node::LateDeferredPrepass,
          Node::CopyDeferredLightingId, Node::EndPrepasses, Node::StartMainPass, Node::MainOpaquePass,
          Node::MainTransmissivePass, Node::MainTransparentPass, Node::EndMainPass, Node::StartMainPassPostProcessing,
          Node::Tonemapping, Node::EndMainPassPostProcessing, Node::Upscaling}) {
        EXPECT_TRUE(core3d->get().get_node_state(node).has_value());
    }
    for (const auto node :
         {Node::MsaaWriteback, Node::EarlyDownsampleDepth, Node::Wireframe, Node::LateDownsampleDepth, Node::MotionBlur,
          Node::Taa, Node::DlssSuperResolution, Node::DlssRayReconstruction, Node::Bloom, Node::AutoExposure,
          Node::DepthOfField, Node::PostProcessing, Node::Fxaa, Node::Smaa, Node::ContrastAdaptiveSharpening}) {
        EXPECT_FALSE(core3d->get().get_node_state(node).has_value());
    }

    const std::array chain{Node::EarlyPrepass,
                           Node::EarlyDeferredPrepass,
                           Node::LatePrepass,
                           Node::LateDeferredPrepass,
                           Node::CopyDeferredLightingId,
                           Node::EndPrepasses,
                           Node::StartMainPass,
                           Node::MainOpaquePass,
                           Node::MainTransmissivePass,
                           Node::MainTransparentPass,
                           Node::EndMainPass,
                           Node::StartMainPassPostProcessing,
                           Node::Tonemapping,
                           Node::EndMainPassPostProcessing,
                           Node::Upscaling};
    for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
        EXPECT_TRUE(core3d->get().has_edge(graph::Edge::node_edge(chain[i], chain[i + 1])))
            << "missing Core3D chain edge at index " << i;
    }
}

// Bevy derives the equality/hash contract of both 3D batch-set keys and reports
// `indexed()` from the presence of an index slab.
TEST(Core3dPhaseKeys, MatchBevyOpaqueAndAlphaMaskBatchSets) {
    using namespace epix::core_graph::core_3d;
    static_assert(phase::PhaseItemBatchSetKey<Opaque3dBatchSetKey>);
    static_assert(phase::PhaseItemBatchSetKey<OpaqueNoLightmap3dBatchSetKey>);

    const Opaque3dBatchSetKey unindexed{.pipeline_id = CachedPipelineId{3}, .draw_func = phase::DrawFunctionId{1}};
    EXPECT_FALSE(unindexed.indexed());
    auto indexed                      = unindexed;
    indexed.index_slab                = epix::render::mesh::SlabId{7};
    indexed.lightmap_slab             = 4u;
    indexed.material_bind_group_index = 2u;
    EXPECT_TRUE(indexed.indexed());
    EXPECT_NE(unindexed, indexed);
    EXPECT_NE(std::hash<Opaque3dBatchSetKey>{}(unindexed), std::hash<Opaque3dBatchSetKey>{}(indexed));

    const OpaqueNoLightmap3dBatchSetKey mask_unindexed{.pipeline_id = CachedPipelineId{3},
                                                       .draw_func   = phase::DrawFunctionId{1}};
    EXPECT_FALSE(mask_unindexed.indexed());
    EXPECT_EQ(std::hash<OpaqueNoLightmap3dBatchSetKey>{}(mask_unindexed),
              std::hash<OpaqueNoLightmap3dBatchSetKey>{}(mask_unindexed));

    // The alpha-mask bin key carries only the asset identity, exactly like
    // Bevy's OpaqueNoLightmap3dBinKey.
    const auto asset = epix::assets::UntypedAssetId(epix::assets::AssetId<std::uint32_t>::invalid());
    const Opaque3dBinKey opaque_bin{.asset_id = asset};
    const OpaqueNoLightmap3dBinKey mask_bin{.asset_id = asset};
    EXPECT_EQ(opaque_bin, opaque_bin);
    EXPECT_EQ(mask_bin, mask_bin);
}

// Bevy: Opaque3d and AlphaMask3d are binned items whose batch-set key owns the
// draw function and cached pipeline; both expose the lightmap-free/prepass key
// pair distinction through distinct aliases.
TEST(Core3dBinnedPhases, CreateFromKeysAndReportDrawData) {
    using namespace epix::core_graph::core_3d;
    static_assert(phase::BinnedPhaseItem<Opaque3d>);
    static_assert(phase::BinnedPhaseItem<AlphaMask3d>);
    static_assert(std::same_as<Opaque3d::BatchSetKey, Opaque3dBatchSetKey>);
    static_assert(std::same_as<AlphaMask3d::BatchSetKey, OpaqueNoLightmap3dBatchSetKey>);
    static_assert(std::same_as<AlphaMask3d::BinKey, OpaqueNoLightmap3dBinKey>);

    const auto entity = Entity::from_index(41);
    const auto asset  = epix::assets::UntypedAssetId(epix::assets::AssetId<std::uint32_t>::invalid());
    const auto extra  = phase::PhaseItemExtraIndex::dynamic_offset(9);

    const auto opaque = Opaque3d::create(
        Opaque3dBatchSetKey{.pipeline_id = CachedPipelineId{11}, .draw_func = phase::DrawFunctionId{2}},
        Opaque3dBinKey{.asset_id = asset}, {entity, sync_world::MainEntity{entity}}, {5, 8}, extra);
    EXPECT_EQ(opaque.entity(), entity);
    EXPECT_EQ(opaque.main_entity(), sync_world::MainEntity{entity});
    EXPECT_EQ(opaque.cached_pipeline(), CachedPipelineId{11});
    EXPECT_EQ(opaque.draw_function(), phase::DrawFunctionId{2});
    EXPECT_EQ(opaque.batch_range(), (std::pair<std::uint32_t, std::uint32_t>{5, 8}));
    EXPECT_EQ(opaque.extra_index(), extra);

    const auto mask = AlphaMask3d::create(
        OpaqueNoLightmap3dBatchSetKey{.pipeline_id = CachedPipelineId{12}, .draw_func = phase::DrawFunctionId{3}},
        OpaqueNoLightmap3dBinKey{.asset_id = asset}, {entity, sync_world::MainEntity{entity}}, {1, 2},
        phase::PhaseItemExtraIndex::None);
    EXPECT_EQ(mask.cached_pipeline(), CachedPipelineId{12});
    EXPECT_EQ(mask.draw_function(), phase::DrawFunctionId{3});
}

// Bevy: Transmissive3d opts out of automatic batching because its items are
// re-drawn per transmission step; Transparent3d keeps the default and both sort
// ascending by view-space distance (back-to-front).
TEST(Core3dSortedPhases, SortBackToFrontAndDisableTransmissiveBatching) {
    using namespace epix::core_graph::core_3d;
    static_assert(phase::SortedPhaseItem<Transmissive3d>);
    static_assert(phase::SortedPhaseItem<Transparent3d>);
    static_assert(!Transmissive3d::AUTOMATIC_BATCHING);
    EXPECT_FALSE(batching::automatic_batching_enabled<Transmissive3d>);
    EXPECT_TRUE(batching::automatic_batching_enabled<Transparent3d>);

    const auto entity = Entity::from_index(7);
    const auto make   = [&entity](float distance, bool indexed) {
        return Transparent3d{.distance          = distance,
                             .pipeline_id       = CachedPipelineId{1},
                             .entity_value      = {entity, sync_world::MainEntity{entity}},
                             .draw_func         = phase::DrawFunctionId{1},
                             .batch_range_value = {0, 1},
                             .indexed_value     = indexed};
    };
    phase::SortedRenderPhase<Transparent3d> phase;
    phase.add(make(3.0f, false));
    phase.add(make(1.0f, true));
    phase.add(make(2.0f, false));
    phase.sort();
    ASSERT_EQ(phase.items.size(), 3u);
    EXPECT_FLOAT_EQ(phase.items[0].distance, 1.0f);
    EXPECT_FLOAT_EQ(phase.items[1].distance, 2.0f);
    EXPECT_FLOAT_EQ(phase.items[2].distance, 3.0f);
    EXPECT_TRUE(phase.items[0].indexed());
    EXPECT_FALSE(phase.items[1].indexed());

    Transmissive3d transmissive{.distance          = 1.5f,
                                .pipeline_id       = CachedPipelineId{2},
                                .entity_value      = {entity, sync_world::MainEntity{entity}},
                                .draw_func         = phase::DrawFunctionId{2},
                                .batch_range_value = {0, 1},
                                .indexed_value     = true};
    EXPECT_FLOAT_EQ(transmissive.sort_key(), 1.5f);
    EXPECT_TRUE(transmissive.indexed());
    EXPECT_EQ(transmissive.cached_pipeline(), CachedPipelineId{2});
}

// Bevy's transmissive node splits its sorted items into at most
// `screen_space_specular_transmission_steps` ranges and hands the remainder to
// the initial ranges.
TEST(SplitTransmissiveRange, DistributesRemainderToInitialRanges) {
    using epix::core_graph::core_3d::split_transmissive_range;

    const auto single = split_transmissive_range(0, 5, 1);
    ASSERT_EQ(single.size(), 1u);
    EXPECT_EQ(single[0], (std::pair<std::size_t, std::size_t>{0, 5}));

    // More steps than items clamps to one item per range.
    const auto clamped = split_transmissive_range(0, 2, 5);
    ASSERT_EQ(clamped.size(), 2u);
    EXPECT_EQ(clamped[0], (std::pair<std::size_t, std::size_t>{0, 1}));
    EXPECT_EQ(clamped[1], (std::pair<std::size_t, std::size_t>{1, 2}));

    // 7 items over 3 steps: sizes 3, 2, 2 (remainder first), contiguous.
    const auto thirds = split_transmissive_range(0, 7, 3);
    ASSERT_EQ(thirds.size(), 3u);
    EXPECT_EQ(thirds[0], (std::pair<std::size_t, std::size_t>{0, 3}));
    EXPECT_EQ(thirds[1], (std::pair<std::size_t, std::size_t>{3, 5}));
    EXPECT_EQ(thirds[2], (std::pair<std::size_t, std::size_t>{5, 7}));

    // A non-zero start offset is preserved.
    const auto offset = split_transmissive_range(2, 4, 2);
    ASSERT_EQ(offset.size(), 2u);
    EXPECT_EQ(offset[0], (std::pair<std::size_t, std::size_t>{2, 3}));
    EXPECT_EQ(offset[1], (std::pair<std::size_t, std::size_t>{3, 4}));

    EXPECT_THROW(split_transmissive_range(3, 3, 2), std::invalid_argument);
    EXPECT_THROW(split_transmissive_range(0, 2, 0), std::invalid_argument);
}

// Bevy Core3dPlugin gives Camera3d the Core3D graph, an enabled DebandDither,
// the default Tonemapping, and extracts the marker into the render world so
// Core3D systems can query it.
TEST(Core3dPlugin, AddsCameraRequirementsAndExtractsCamera3d) {
    epix::app::App app = epix::app::App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::app::TaskPoolPlugin{})
        .add_plugins(epix::time::TimePlugin{})
        .add_plugins(epix::render::FrameCountPlugin{})
        .add_plugins(epix::camera::CameraPlugin{})
        .add_plugins(epix::assets::AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{});
    try {
        app.add_plugins(RenderPlugin{});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan unavailable: " << error.what();
    }
    app.add_plugins(epix::core_graph::core_3d::Core3dPlugin{});
    ASSERT_TRUE(app.world().get_resource<FrameCount>().has_value());

    const Entity camera = app.world_mut().spawn(::epix::camera::Camera3d{}).id();
    const auto main     = app.world().get_entity(camera);
    ASSERT_TRUE(main.has_value());
    EXPECT_TRUE(main->contains<::epix::camera::Camera>());
    EXPECT_TRUE(main->contains<epix::core_graph::DebandDither>());
    EXPECT_TRUE(main->contains<epix::core_graph::Tonemapping>());
    EXPECT_TRUE(main->contains<epix::render::camera::CameraRenderGraph>());
    EXPECT_EQ(main->get<epix::core_graph::DebandDither>()->get(), epix::core_graph::DebandDither::Enabled);
    EXPECT_EQ(main->get<epix::core_graph::Tonemapping>()->get(), epix::core_graph::Tonemapping::TonyMcMapface);
    EXPECT_EQ(main->get<epix::render::camera::CameraRenderGraph>()->get(),
              epix::render::graph::GraphLabel(epix::core_graph::core_3d::Core3d));

    // Phase ownership follows Camera3d, not the chosen graph, and a missing
    // graph must not suppress extraction.
    const Entity custom_graph =
        app.world_mut()
            .spawn(::epix::camera::Camera3d{},
                   epix::render::camera::CameraRenderGraph{epix::core_graph::core_3d::Node3d::Wireframe})
            .id();
    const Entity no_graph = app.world_mut().spawn(::epix::camera::Camera3d{}).id();
    app.world_mut().entity_mut(no_graph).remove<epix::render::camera::CameraRenderGraph>();
    ::epix::camera::Camera inactive_camera;
    inactive_camera.is_active = false;
    const Entity inactive     = app.world_mut().spawn(::epix::camera::Camera3d{}, inactive_camera).id();
    const Entity non_3d       = app.world_mut().spawn(::epix::camera::Camera{}).id();

    auto render_sub = app.take_sub_app(Render);
    ASSERT_TRUE(render_sub);
    render_sub->extract(app);
    render_sub->resource_scope([](epix::ecs::Schedules& schedules, epix::ecs::World& world) {
        schedules.schedule_mut(ExtractSchedule).apply_deferred(world);
    });
    const auto render_entity =
        app.world()
            .get_entity(camera)
            .and_then([](const EntityRef& entity) { return entity.get<sync_world::RenderEntity>(); })
            .transform([](const auto& value) { return value.get().id(); });
    ASSERT_TRUE(render_entity.has_value());
    const auto extracted = render_sub->world().get_entity(*render_entity);
    ASSERT_TRUE(extracted.has_value());
    EXPECT_TRUE(extracted->contains<::epix::camera::Camera3d>());

    const auto check_phases = [&](Entity entity, bool expected) {
        const auto retained = view::RetainedViewEntity::create(sync_world::MainEntity{entity}, std::nullopt, 0);
        using namespace epix::core_graph::core_3d;
        EXPECT_EQ(render_sub->resource<phase::ViewBinnedRenderPhases<Opaque3d>>().phases.contains(retained), expected);
        EXPECT_EQ(render_sub->resource<phase::ViewBinnedRenderPhases<AlphaMask3d>>().phases.contains(retained),
                  expected);
        EXPECT_EQ(render_sub->resource<phase::ViewSortedRenderPhases<Transmissive3d>>().contains(retained), expected);
        EXPECT_EQ(render_sub->resource<phase::ViewSortedRenderPhases<Transparent3d>>().contains(retained), expected);
    };
    for (Entity entity : {camera, custom_graph, no_graph}) check_phases(entity, true);
    for (Entity entity : {inactive, non_3d}) check_phases(entity, false);

    app.world_mut().entity_mut(custom_graph).insert(inactive_camera);
    app.world_mut().entity_mut(no_graph).despawn();
    render_sub->extract(app);
    check_phases(camera, true);
    check_phases(custom_graph, false);
    check_phases(no_graph, false);
    app.insert_sub_app(Render, std::move(render_sub));
}

namespace {

/** @brief Register the four Core3D phases plus a depth cache on a test render
 * world, mirroring what Core3dPlugin initializes. */
void init_core3d_test_resources(epix::ecs::World& world) {
    using namespace epix::core_graph::core_3d;
    world.init_resource<phase::ViewBinnedRenderPhases<Opaque3d>>();
    world.init_resource<phase::ViewBinnedRenderPhases<AlphaMask3d>>();
    world.init_resource<phase::ViewSortedRenderPhases<Transmissive3d>>();
    world.init_resource<phase::ViewSortedRenderPhases<Transparent3d>>();
}

/** @brief Spawn a render-world Camera3d-carrying view and optionally give it
 * membership in the four Core3D phases. */
Core3dTestView spawn_core3d_view(epix::ecs::World& world,
                                 ::epix::camera::Camera3d camera_3d,
                                 std::optional<::epix::camera::NormalizedRenderTarget> target,
                                 std::optional<glm::uvec2> size,
                                 view::Msaa msaa,
                                 bool opaque,
                                 bool alpha_mask,
                                 bool transmissive,
                                 bool transparent,
                                 bool hdr = false) {
    using namespace epix::core_graph::core_3d;
    auto entity         = world.spawn();
    const auto id       = entity.id();
    const auto retained = view::RetainedViewEntity::create(sync_world::MainEntity{id}, std::nullopt, 0);
    entity.insert(camera::ExtractedCamera{.target = target, .physical_target_size = size},
                  view::ExtractedView{.retained_view_entity = retained, .hdr = hdr}, msaa, camera_3d);
    if (opaque) {
        world.resource_mut<phase::ViewBinnedRenderPhases<Opaque3d>>().prepare_for_new_frame(
            retained, batching::GpuPreprocessingMode::None);
    }
    if (alpha_mask) {
        world.resource_mut<phase::ViewBinnedRenderPhases<AlphaMask3d>>().prepare_for_new_frame(
            retained, batching::GpuPreprocessingMode::None);
    }
    if (transmissive) world.resource_mut<phase::ViewSortedRenderPhases<Transmissive3d>>().insert_or_clear(retained);
    if (transparent) world.resource_mut<phase::ViewSortedRenderPhases<Transparent3d>>().insert_or_clear(retained);
    return Core3dTestView{.entity = id, .retained = retained};
}

}  // namespace

// Bevy prepare_core_3d_depth_textures unions the requested usages per output
// target, caches one depth texture per (target, MSAA), and honours the
// camera's clear-vs-load depth policy.
TEST(Core3dDepthTextures, UnionsUsagesPerTargetAndHonorsLoadOp) {
    using namespace epix::core_graph::core_3d;
    epix::app::App app = epix::app::App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::app::TaskPoolPlugin{})
        .add_plugins(epix::time::TimePlugin{})
        .add_plugins(epix::camera::CameraPlugin{})
        .add_plugins(epix::assets::AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{});
    try {
        app.add_plugins(RenderPlugin{});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan unavailable: " << error.what();
    }
    auto& render_app = app.get_sub_app_mut(Render)->get();
    auto& world      = render_app.world_mut();
    init_core3d_test_resources(world);
    render_app.add_systems(epix::app::Update, into(prepare_core_3d_depth_textures));

    const ::epix::camera::NormalizedRenderTarget shared_target{::epix::camera::ManualTextureViewHandle{42}};
    const ::epix::camera::NormalizedRenderTarget other_target{::epix::camera::ManualTextureViewHandle{43}};

    // Same target, different requested usages: the created texture must carry
    // the union, and both cameras share it.
    ::epix::camera::Camera3d attachment_only;
    ::epix::camera::Camera3d sampled;
    sampled.depth_texture_usages = ::epix::camera::Camera3dDepthTextureUsage(wgpu::TextureUsage::eRenderAttachment |
                                                                             wgpu::TextureUsage::eTextureBinding);
    const auto first = spawn_core3d_view(world, attachment_only, shared_target, glm::uvec2{64, 32}, view::Msaa::Off,
                                         true, true, true, true);
    const auto second =
        spawn_core3d_view(world, sampled, shared_target, glm::uvec2{64, 32}, view::Msaa::Off, true, true, true, true);
    // Same target but a different MSAA sample count gets its own depth texture.
    const auto multisampled = spawn_core3d_view(world, attachment_only, shared_target, glm::uvec2{64, 32},
                                                view::Msaa::Sample4, true, true, true, true);
    // Independent target: usage stays exactly what that camera requested.
    const auto independent =
        spawn_core3d_view(world, sampled, other_target, glm::uvec2{16, 8}, view::Msaa::Off, true, true, true, true);
    // Rejected views: no Camera3d-independent phase membership or no size.
    const auto no_phases = spawn_core3d_view(world, attachment_only, shared_target, glm::uvec2{64, 32}, view::Msaa::Off,
                                             false, true, true, true);
    const auto no_size =
        spawn_core3d_view(world, attachment_only, shared_target, std::nullopt, view::Msaa::Off, true, true, true, true);

    render_app.run_schedule(epix::app::Update);

    for (Entity entity : {no_phases.entity, no_size.entity}) {
        EXPECT_FALSE(world.entity(entity).contains<view::ViewDepthTexture>());
    }
    const auto first_depth        = world.entity(first.entity).get<view::ViewDepthTexture>();
    const auto second_depth       = world.entity(second.entity).get<view::ViewDepthTexture>();
    const auto multisampled_depth = world.entity(multisampled.entity).get<view::ViewDepthTexture>();
    const auto independent_depth  = world.entity(independent.entity).get<view::ViewDepthTexture>();
    ASSERT_TRUE(first_depth);
    ASSERT_TRUE(second_depth);
    ASSERT_TRUE(multisampled_depth);
    ASSERT_TRUE(independent_depth);
    EXPECT_EQ(first_depth->get().texture, second_depth->get().texture);
    EXPECT_NE(first_depth->get().texture, multisampled_depth->get().texture);
    EXPECT_NE(first_depth->get().texture, independent_depth->get().texture);
    EXPECT_EQ(first_depth->get().texture.getFormat(), CORE_3D_DEPTH_FORMAT);
    EXPECT_EQ(first_depth->get().texture.getWidth(), 64u);
    EXPECT_EQ(first_depth->get().texture.getHeight(), 32u);
    EXPECT_EQ(first_depth->get().texture.getUsage(),
              wgpu::TextureUsage::eRenderAttachment | wgpu::TextureUsage::eTextureBinding);
    EXPECT_EQ(multisampled_depth->get().texture.getSampleCount(), 4u);
    EXPECT_EQ(independent_depth->get().texture.getUsage(),
              wgpu::TextureUsage::eRenderAttachment | wgpu::TextureUsage::eTextureBinding);

    // Camera3dDepthLoadOp::Clear(0.25) clears on the first attachment build;
    // every later build loads. The default adds a Load-op camera on another
    // target that must never clear.
    ::epix::camera::Camera3d load_camera;
    load_camera.depth_load_op = ::epix::camera::Camera3dDepthLoadOp{::epix::camera::Camera3dDepthLoadOp::Load{}};
    const auto loading        = spawn_core3d_view(
        world, load_camera, ::epix::camera::NormalizedRenderTarget{::epix::camera::ManualTextureViewHandle{44}},
        glm::uvec2{8, 8}, view::Msaa::Off, true, true, true, true);
    render_app.run_schedule(epix::app::Update);
    const auto loading_depth = world.entity(loading.entity).get<view::ViewDepthTexture>();
    ASSERT_TRUE(loading_depth);
    const auto load_attachment = loading_depth->get().get_attachment(wgpu::StoreOp::eStore);
    EXPECT_EQ(load_attachment.depthLoadOp, wgpu::LoadOp::eLoad);
    EXPECT_EQ(load_attachment.depthClearValue, 0.0f);

    const auto first_attachment = first_depth->get().get_attachment(wgpu::StoreOp::eStore);
    EXPECT_EQ(first_attachment.depthLoadOp, wgpu::LoadOp::eClear);
    EXPECT_EQ(first_attachment.depthClearValue, 0.0f);
    EXPECT_EQ(first_depth->get().get_attachment(wgpu::StoreOp::eStore).depthLoadOp, wgpu::LoadOp::eLoad);
}

// Bevy prepare_core_3d_transmission_textures creates a single-sample copy
// destination per target in the view's HDR/LDR format, but only when all four
// phases exist, the transmissive work is non-empty, the step count is non-zero,
// and the camera has a physical target size.
TEST(Core3dTransmissionTextures, RequiresStepsItemsPhasesAndSize) {
    using namespace epix::core_graph::core_3d;
    epix::app::App app = epix::app::App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::app::TaskPoolPlugin{})
        .add_plugins(epix::time::TimePlugin{})
        .add_plugins(epix::camera::CameraPlugin{})
        .add_plugins(epix::assets::AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{});
    try {
        app.add_plugins(RenderPlugin{});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan unavailable: " << error.what();
    }
    auto& render_app = app.get_sub_app_mut(Render)->get();
    auto& world      = render_app.world_mut();
    init_core3d_test_resources(world);
    render_app.add_systems(epix::app::Update, into(prepare_core_3d_transmission_textures));

    const auto transmissive_item = [](Entity entity) {
        return Transmissive3d{.distance          = 1.0f,
                              .pipeline_id       = CachedPipelineId{1},
                              .entity_value      = {entity, sync_world::MainEntity{entity}},
                              .draw_func         = phase::DrawFunctionId{1},
                              .batch_range_value = {0, 1}};
    };

    const ::epix::camera::NormalizedRenderTarget ldr_target{::epix::camera::ManualTextureViewHandle{51}};
    const ::epix::camera::NormalizedRenderTarget hdr_target{::epix::camera::ManualTextureViewHandle{52}};
    ::epix::camera::Camera3d one_step;
    ::epix::camera::Camera3d zero_steps;
    zero_steps.screen_space_specular_transmission_steps = 0;

    const auto ldr = spawn_core3d_view(world, one_step, ldr_target, glm::uvec2{40, 20}, view::Msaa::Off, true, true,
                                       true, true, false);
    world.resource_mut<phase::ViewSortedRenderPhases<Transmissive3d>>()
        .at(ldr.retained)
        .items.push_back(transmissive_item(ldr.entity));
    // A second camera on the same target must observe the identical cached
    // transmission texture within the same frame.
    const auto ldr_shared = spawn_core3d_view(world, one_step, ldr_target, glm::uvec2{40, 20}, view::Msaa::Off, true,
                                              true, true, true, false);
    world.resource_mut<phase::ViewSortedRenderPhases<Transmissive3d>>()
        .at(ldr_shared.retained)
        .items.push_back(transmissive_item(ldr_shared.entity));
    const auto hdr = spawn_core3d_view(world, one_step, hdr_target, glm::uvec2{40, 20}, view::Msaa::Off, true, true,
                                       true, true, true);
    world.resource_mut<phase::ViewSortedRenderPhases<Transmissive3d>>()
        .at(hdr.retained)
        .items.push_back(transmissive_item(hdr.entity));

    // Skip cases.
    const auto missing_phase =
        spawn_core3d_view(world, one_step, ldr_target, glm::uvec2{40, 20}, view::Msaa::Off, true, true, true, false);
    const auto no_items =
        spawn_core3d_view(world, one_step, ldr_target, glm::uvec2{40, 20}, view::Msaa::Off, true, true, true, true);
    const auto no_steps =
        spawn_core3d_view(world, zero_steps, ldr_target, glm::uvec2{40, 20}, view::Msaa::Off, true, true, true, true);
    world.resource_mut<phase::ViewSortedRenderPhases<Transmissive3d>>()
        .at(no_steps.retained)
        .items.push_back(transmissive_item(no_steps.entity));
    const auto no_size =
        spawn_core3d_view(world, one_step, ldr_target, std::nullopt, view::Msaa::Off, true, true, true, true);
    world.resource_mut<phase::ViewSortedRenderPhases<Transmissive3d>>()
        .at(no_size.retained)
        .items.push_back(transmissive_item(no_size.entity));

    render_app.run_schedule(epix::app::Update);

    const auto ldr_texture = world.entity(ldr.entity).get<ViewTransmissionTexture>();
    const auto hdr_texture = world.entity(hdr.entity).get<ViewTransmissionTexture>();
    ASSERT_TRUE(ldr_texture);
    ASSERT_TRUE(hdr_texture);
    EXPECT_EQ(ldr_texture->get().texture.getFormat(), wgpu::TextureFormat::eRGBA8UnormSrgb);
    EXPECT_EQ(hdr_texture->get().texture.getFormat(), view::ViewTarget::TEXTURE_FORMAT_HDR);
    EXPECT_EQ(ldr_texture->get().texture.getWidth(), 40u);
    EXPECT_EQ(ldr_texture->get().texture.getHeight(), 20u);
    // Single-sample copy destination that is sampleable.
    EXPECT_EQ(ldr_texture->get().texture.getSampleCount(), 1u);
    EXPECT_EQ(ldr_texture->get().texture.getUsage(),
              wgpu::TextureUsage::eTextureBinding | wgpu::TextureUsage::eCopyDst);
    EXPECT_TRUE(static_cast<bool>(ldr_texture->get().view));
    EXPECT_TRUE(static_cast<bool>(ldr_texture->get().sampler));

    for (Entity entity : {missing_phase.entity, no_items.entity, no_steps.entity, no_size.entity}) {
        EXPECT_FALSE(world.entity(entity).contains<ViewTransmissionTexture>());
    }
    const auto shared_texture = world.entity(ldr_shared.entity).get<ViewTransmissionTexture>();
    ASSERT_TRUE(shared_texture);
    EXPECT_EQ(shared_texture->get().texture, ldr_texture->get().texture);
}

// C14: Bevy's prepass phase items are binned and share the lightmap-free key
// pair with `AlphaMask3d` (`prepass/mod.rs:175-380`), and the two prepass
// formats match `prepass/mod.rs:52-53`.
TEST(Core3dPrepassPhases, MatchBevyBinnedItemContractsAndFormats) {
    using namespace epix::core_graph::core_3d;
    static_assert(phase::BinnedPhaseItem<Opaque3dPrepass>);
    static_assert(phase::BinnedPhaseItem<AlphaMask3dPrepass>);
    static_assert(phase::CachedRenderPipelinePhaseItem<Opaque3dPrepass>);
    static_assert(phase::CachedRenderPipelinePhaseItem<AlphaMask3dPrepass>);
    static_assert(std::same_as<Opaque3dPrepass::BatchSetKey, OpaqueNoLightmap3dBatchSetKey>);
    static_assert(std::same_as<Opaque3dPrepass::BinKey, OpaqueNoLightmap3dBinKey>);
    static_assert(std::same_as<AlphaMask3dPrepass::BatchSetKey, OpaqueNoLightmap3dBatchSetKey>);
    static_assert(std::same_as<AlphaMask3dPrepass::BinKey, OpaqueNoLightmap3dBinKey>);

    EXPECT_EQ(epix::core_graph::core_3d::NORMAL_PREPASS_FORMAT, wgpu::TextureFormat::eRGB10A2Unorm);
    EXPECT_EQ(epix::core_graph::core_3d::MOTION_VECTOR_PREPASS_FORMAT, wgpu::TextureFormat::eRG16Float);
}

// C13-D3/C15: Bevy registers `check_msaa` in the main world's `PostUpdate`, and
// it forces every camera carrying `DeferredPrepass` to `Msaa::Off` because MSAA
// is incompatible with deferred rendering (`core_3d/mod.rs:151,978-988`). A
// camera that already asked for `Off` is left alone, and a camera without
// `DeferredPrepass` keeps whatever it asked for.
TEST(Core3dMsaa, DisablesMsaaOnlyForDeferredViews) {
    epix::app::App app = epix::app::App::create();
    app.add_plugins(epix::app::TaskPoolPlugin{}).add_plugins(epix::time::TimePlugin{});
    app.add_plugins(epix::core_graph::core_3d::Core3dPlugin{});

    using namespace epix::core_graph::core_3d;
    const auto spawn = [&app](view::Msaa msaa, bool deferred) {
        auto entity = app.world_mut().spawn(::epix::camera::Camera3d{}, msaa);
        if (deferred) entity.insert(DeferredPrepass{});
        return entity.id();
    };
    const auto deferred_msaa = spawn(view::Msaa::Sample4, true);
    const auto deferred_off  = spawn(view::Msaa::Off, true);
    const auto plain_msaa    = spawn(view::Msaa::Sample4, false);

    app.run_schedule(epix::app::PostUpdate);

    const auto msaa_of = [&](Entity entity) {
        return app.world().get_entity(entity).and_then(
            [](const EntityRef& ref) { return ref.get<view::Msaa>(); });
    };
    EXPECT_EQ(msaa_of(deferred_msaa).value().get(), view::Msaa::Off);
    EXPECT_EQ(msaa_of(deferred_off).value().get(), view::Msaa::Off);
    EXPECT_EQ(msaa_of(plain_msaa).value().get(), view::Msaa::Sample4);
}

// CGR-116/CGR-185: Bevy `configure_occlusion_culling_view_targets`
// (`core_3d/mod.rs:960-975`) adds `TEXTURE_BINDING` to the depth-texture usage of
// every view that runs occlusion culling - the culling pass samples that texture.
// The three filters are all load-bearing: without `DepthPrepass` there is no
// depth texture to sample, and `NoIndirectDrawing` opts the view out of the
// culling path entirely.
TEST(Core3dOcclusionCullingTargets, AddsTextureBindingOnlyToCullingViews) {
    epix::app::App app = epix::app::App::create();
    app.add_plugins(epix::app::TaskPoolPlugin{}).add_plugins(epix::time::TimePlugin{});
    app.add_systems(epix::app::Update,
                    epix::ecs::into(epix::core_graph::core_3d::configure_occlusion_culling_view_targets));

    using namespace epix::core_graph::core_3d;
    const auto spawn = [&app](bool culling, bool no_indirect_drawing, bool depth_prepass) {
        ::epix::camera::Camera3d camera_3d;
        camera_3d.depth_texture_usages =
            ::epix::camera::Camera3dDepthTextureUsage(wgpu::TextureUsage::eRenderAttachment);
        auto entity = app.world_mut().spawn(camera_3d);
        if (culling) entity.insert(epix::render::experimental::OcclusionCulling{});
        if (no_indirect_drawing) entity.insert(::epix::render::view::NoIndirectDrawing{});
        if (depth_prepass) entity.insert(DepthPrepass{});
        return entity.id();
    };
    const auto culling            = spawn(true, false, true);
    const auto no_indirect        = spawn(true, true, true);
    const auto no_depth_prepass   = spawn(true, false, false);
    const auto no_occlusion       = spawn(false, false, true);

    app.update();

    const auto usage_bits_of = [&](Entity entity) {
        const auto camera_3d = app.world().get_entity(entity).and_then(
            [](const EntityRef& ref) { return ref.get<::epix::camera::Camera3d>(); });
        return camera_3d.value().get().depth_texture_usages.bits;
    };
    const auto sampler_flag = static_cast<std::uint32_t>(wgpu::TextureUsage::eTextureBinding);
    EXPECT_NE(usage_bits_of(culling) & sampler_flag, 0u)
        << "a culling view's depth texture has to be sampleable";
    EXPECT_EQ(usage_bits_of(no_indirect) & sampler_flag, 0u);
    EXPECT_EQ(usage_bits_of(no_depth_prepass) & sampler_flag, 0u);
    EXPECT_EQ(usage_bits_of(no_occlusion) & sampler_flag, 0u);
}

// C14/C15: `extract_camera_prepass_phase` is Bevy's single extraction system for
// both the prepass and the deferred phases. It prepares each binned phase pair
// only for an active camera that asked for the matching component, drops the
// phases of a camera that asked for neither, and re-inserts (or removes) the
// prepass marker components on the camera's synced render entity
// (`core_3d/mod.rs:691-788`). The system runs in the render world's
// ExtractSchedule, so the whole render sub-app is built and extracted exactly as
// the Core3dPlugin test does.
TEST(Core3dPrepassExtraction, RequiresAnActiveCameraWithAPrepassComponent) {
    epix::app::App app = epix::app::App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::app::TaskPoolPlugin{})
        .add_plugins(epix::time::TimePlugin{})
        .add_plugins(epix::render::FrameCountPlugin{})
        .add_plugins(epix::camera::CameraPlugin{})
        .add_plugins(epix::assets::AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{});
    try {
        app.add_plugins(RenderPlugin{});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan unavailable: " << error.what();
    }
    app.add_plugins(epix::core_graph::core_3d::Core3dPlugin{});

    const auto spawn = [&app](bool active, bool depth_prepass, bool normal_prepass, bool deferred_prepass) {
        ::epix::camera::Camera camera;
        camera.is_active = active;
        auto entity      = app.world_mut().spawn(::epix::camera::Camera3d{}, camera);
        if (depth_prepass) entity.insert(epix::core_graph::core_3d::DepthPrepass{});
        if (normal_prepass) entity.insert(epix::core_graph::core_3d::NormalPrepass{});
        if (deferred_prepass) entity.insert(epix::core_graph::core_3d::DeferredPrepass{});
        return entity.id();
    };

    const auto with_depth    = spawn(true, true, false, false);
    const auto with_normal   = spawn(true, false, true, false);
    const auto with_deferred = spawn(true, false, false, true);
    const auto none          = spawn(true, false, false, false);
    const auto inactive      = spawn(false, true, true, true);

    auto render_sub = app.take_sub_app(Render);
    ASSERT_TRUE(render_sub);
    render_sub->extract(app);
    render_sub->resource_scope([](epix::ecs::Schedules& schedules, epix::ecs::World& world) {
        schedules.schedule_mut(ExtractSchedule).apply_deferred(world);
    });

    const auto check = [&](Entity entity, bool prepass_expected, bool deferred_expected) {
        using namespace epix::core_graph::core_3d;
        const auto retained = view::RetainedViewEntity::create(sync_world::MainEntity{entity}, std::nullopt, 0);
        EXPECT_EQ(render_sub->resource<phase::ViewBinnedRenderPhases<Opaque3dPrepass>>().phases.contains(retained),
                  prepass_expected);
        EXPECT_EQ(render_sub->resource<phase::ViewBinnedRenderPhases<AlphaMask3dPrepass>>().phases.contains(retained),
                  prepass_expected);
        EXPECT_EQ(render_sub->resource<phase::ViewBinnedRenderPhases<Opaque3dDeferred>>().phases.contains(retained),
                  deferred_expected);
        EXPECT_EQ(render_sub->resource<phase::ViewBinnedRenderPhases<AlphaMask3dDeferred>>().phases.contains(retained),
                  deferred_expected);
    };
    check(with_depth, true, false);
    check(with_normal, true, false);
    check(with_deferred, false, true);
    // A camera that asks for neither gets no phases at all.
    check(none, false, false);
    // An inactive camera is skipped before either condition is even read.
    check(inactive, false, false);

    // Bevy copies the prepass markers onto the synced render entity, which is
    // what the render-world preparation and pass nodes query.
    const auto render_entity_of = [&](Entity entity) {
        return app.world()
            .get_entity(entity)
            .and_then([](const EntityRef& ref) { return ref.get<sync_world::RenderEntity>(); })
            .transform([](const auto& value) { return value.get().id(); });
    };
    using namespace epix::core_graph::core_3d;
    const auto check_markers = [&](Entity entity, bool depth, bool normal, bool deferred) {
        const auto render_entity = render_entity_of(entity);
        ASSERT_TRUE(render_entity.has_value());
        const auto extracted = render_sub->world().get_entity(*render_entity);
        ASSERT_TRUE(extracted.has_value());
        EXPECT_EQ(extracted->contains<DepthPrepass>(), depth);
        EXPECT_EQ(extracted->contains<NormalPrepass>(), normal);
        EXPECT_EQ(extracted->contains<MotionVectorPrepass>(), false);
        EXPECT_EQ(extracted->contains<DeferredPrepass>(), deferred);
    };
    check_markers(with_depth, true, false, false);
    check_markers(with_normal, false, true, false);
    check_markers(with_deferred, false, false, true);
    check_markers(none, false, false, false);
    // The inactive camera's markers are removed too, so a camera that turns
    // inactive stops contributing prepass state.
    check_markers(inactive, false, false, false);

    app.insert_sub_app(Render, std::move(render_sub));
}

// C14: `prepare_prepass_textures` allocates exactly the textures the requested
// prepass components need, with Bevy's formats, sample counts, and usages, and
// removes the component for a view that is in no prepass phase
// (`core_3d/mod.rs:1033-1221`).
TEST(Core3dPrepassTextures, AllocatesOnlyRequestedTexturesWithBevyFormats) {
    using namespace epix::core_graph::core_3d;
    epix::app::App app = epix::app::App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::app::TaskPoolPlugin{})
        .add_plugins(epix::time::TimePlugin{})
        .add_plugins(epix::camera::CameraPlugin{})
        .add_plugins(epix::assets::AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{});
    try {
        app.add_plugins(RenderPlugin{});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan unavailable: " << error.what();
    }
    auto& render_app = app.get_sub_app_mut(Render)->get();
    auto& world      = render_app.world_mut();
    init_core3d_test_resources(world);
    world.init_resource<batching::GpuPreprocessingSupport>();
    world.init_resource<phase::ViewBinnedRenderPhases<Opaque3dPrepass>>();
    world.init_resource<phase::ViewBinnedRenderPhases<AlphaMask3dPrepass>>();
    world.init_resource<phase::ViewBinnedRenderPhases<Opaque3dDeferred>>();
    world.init_resource<phase::ViewBinnedRenderPhases<AlphaMask3dDeferred>>();
    render_app.add_systems(epix::app::Update, into(prepare_prepass_textures));

    const ::epix::camera::NormalizedRenderTarget target{::epix::camera::ManualTextureViewHandle{77}};

    /** @brief Spawn a view with the requested prepass components and membership. */
    const auto spawn = [&](std::optional<glm::uvec2> size,
                           view::Msaa msaa,
                           bool in_phase,
                           bool depth_prepass,
                           bool normal_prepass,
                           bool motion_vectors) {
        auto entity         = world.spawn();
        const auto id       = entity.id();
        const auto retained = view::RetainedViewEntity::create(sync_world::MainEntity{id}, std::nullopt, 0);
        entity.insert(camera::ExtractedCamera{.target = target, .physical_target_size = size},
                      view::ExtractedView{.retained_view_entity = retained}, msaa);
        if (depth_prepass) entity.insert(DepthPrepass{});
        if (normal_prepass) entity.insert(NormalPrepass{});
        if (motion_vectors) entity.insert(MotionVectorPrepass{});
        if (in_phase) {
            world.resource_mut<phase::ViewBinnedRenderPhases<Opaque3dPrepass>>().prepare_for_new_frame(
                retained, batching::GpuPreprocessingMode::None);
        }
        return Core3dTestView{.entity = id, .retained = retained};
    };

    const auto all_three = spawn(glm::uvec2{64, 48}, view::Msaa::Off, true, true, true, true);
    const auto depth_only = spawn(glm::uvec2{64, 48}, view::Msaa::Off, true, true, false, false);
    const auto sampled    = spawn(glm::uvec2{64, 48}, view::Msaa::Sample4, true, true, false, false);
    const auto not_in_phase = spawn(glm::uvec2{64, 48}, view::Msaa::Off, false, true, true, true);
    const auto no_size      = spawn(std::nullopt, view::Msaa::Off, true, true, true, true);
    // A second view on the same target and sample count, spawned before the run
    // so both are served by one frame's cache.
    const auto shared_same_frame = spawn(glm::uvec2{64, 48}, view::Msaa::Off, true, true, false, false);

    render_app.run_schedule(epix::app::Update);

    const auto all_textures = world.entity(all_three.entity).get<ViewPrepassTextures>();
    ASSERT_TRUE(all_textures);
    EXPECT_EQ(all_textures->get().size.width, 64u);
    EXPECT_EQ(all_textures->get().size.height, 48u);
    ASSERT_TRUE(all_textures->get().depth);
    ASSERT_TRUE(all_textures->get().normal);
    ASSERT_TRUE(all_textures->get().motion_vectors);
    // C15 owns the deferred textures: this view carries no `DeferredPrepass`.
    EXPECT_FALSE(all_textures->get().deferred);
    EXPECT_FALSE(all_textures->get().deferred_lighting_pass_id);
    EXPECT_EQ(all_textures->get().depth->texture.texture.getFormat(), CORE_3D_DEPTH_FORMAT);
    EXPECT_EQ(all_textures->get().normal->texture.texture.getFormat(), NORMAL_PREPASS_FORMAT);
    EXPECT_EQ(all_textures->get().motion_vectors->texture.texture.getFormat(), MOTION_VECTOR_PREPASS_FORMAT);
    // The prepass depth texture is a sampleable copy destination.
    EXPECT_EQ(all_textures->get().depth->texture.texture.getUsage(),
              wgpu::TextureUsage::eCopyDst | wgpu::TextureUsage::eRenderAttachment |
                  wgpu::TextureUsage::eTextureBinding);
    EXPECT_TRUE(all_textures->get().depth_view().has_value());
    EXPECT_TRUE(all_textures->get().normal_view().has_value());
    EXPECT_TRUE(all_textures->get().motion_vectors_view().has_value());
    EXPECT_FALSE(all_textures->get().deferred_view().has_value());

    // Only the requested textures exist.
    const auto depth_only_textures = world.entity(depth_only.entity).get<ViewPrepassTextures>();
    ASSERT_TRUE(depth_only_textures);
    EXPECT_TRUE(depth_only_textures->get().depth);
    EXPECT_FALSE(depth_only_textures->get().normal);
    EXPECT_FALSE(depth_only_textures->get().motion_vectors);

    // Bevy keys the prepass caches by `camera.target` alone
    // (`.entry(camera.target.clone())`, `core_3d/mod.rs:1049`), so a view already
    // in the cache shares that target's texture and therefore whatever sample
    // count the first view created it with. Here the first view for this target
    // was `all_three` (`Msaa::Off`), so the `Sample4` view shares its texture.
    const auto sampled_textures = world.entity(sampled.entity).get<ViewPrepassTextures>();
    ASSERT_TRUE(sampled_textures);
    ASSERT_TRUE(sampled_textures->get().depth);
    EXPECT_EQ(sampled_textures->get().depth->texture.texture, all_textures->get().depth->texture.texture);
    EXPECT_EQ(sampled_textures->get().depth->texture.texture.getSampleCount(), 1u);

    // A view in no prepass phase loses the component, and a view without a
    // physical target size is skipped.
    EXPECT_FALSE(world.entity(not_in_phase.entity).contains<ViewPrepassTextures>());
    EXPECT_FALSE(world.entity(no_size.entity).contains<ViewPrepassTextures>());

    // Views sharing an output target and sample count share one cached texture
    // within a frame.
    const auto shared_textures = world.entity(shared_same_frame.entity).get<ViewPrepassTextures>();
    ASSERT_TRUE(shared_textures);
    ASSERT_TRUE(shared_textures->get().depth);
    EXPECT_EQ(shared_textures->get().depth->texture.texture, depth_only_textures->get().depth->texture.texture);
}

// C13-D5, closed by C14: a view with `DepthPrepass` adds COPY_SRC to the shared
// depth texture's usage so the prepass node can copy it
// (`prepass/node.rs:244-249`).
TEST(Core3dPrepassTextures, DepthPrepassAddsCopySrcToTheViewDepthTexture) {
    using namespace epix::core_graph::core_3d;
    epix::app::App app = epix::app::App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::app::TaskPoolPlugin{})
        .add_plugins(epix::time::TimePlugin{})
        .add_plugins(epix::camera::CameraPlugin{})
        .add_plugins(epix::assets::AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{});
    try {
        app.add_plugins(RenderPlugin{});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan unavailable: " << error.what();
    }
    auto& render_app = app.get_sub_app_mut(Render)->get();
    auto& world      = render_app.world_mut();
    init_core3d_test_resources(world);
    render_app.add_systems(epix::app::Update, into(prepare_core_3d_depth_textures));

    const ::epix::camera::NormalizedRenderTarget target{::epix::camera::ManualTextureViewHandle{91}};
    ::epix::camera::Camera3d camera_3d;

    const auto plain = spawn_core3d_view(world, camera_3d, target, glm::uvec2{32, 16}, view::Msaa::Off, true, true,
                                         true, true);
    const auto with_prepass = [&] {
        auto entity         = world.spawn();
        const auto id       = entity.id();
        const auto retained = view::RetainedViewEntity::create(sync_world::MainEntity{id}, std::nullopt, 0);
        entity.insert(camera::ExtractedCamera{.target = target, .physical_target_size = glm::uvec2{32, 16}},
                      view::ExtractedView{.retained_view_entity = retained}, view::Msaa::Off, camera_3d, DepthPrepass{});
        world.resource_mut<phase::ViewBinnedRenderPhases<Opaque3d>>().prepare_for_new_frame(
            retained, batching::GpuPreprocessingMode::None);
        world.resource_mut<phase::ViewBinnedRenderPhases<AlphaMask3d>>().prepare_for_new_frame(
            retained, batching::GpuPreprocessingMode::None);
        world.resource_mut<phase::ViewSortedRenderPhases<Transmissive3d>>().insert_or_clear(retained);
        world.resource_mut<phase::ViewSortedRenderPhases<Transparent3d>>().insert_or_clear(retained);
        return Core3dTestView{.entity = id, .retained = retained};
    }();

    render_app.run_schedule(epix::app::Update);

    const auto plain_depth   = world.entity(plain.entity).get<view::ViewDepthTexture>();
    const auto prepass_depth = world.entity(with_prepass.entity).get<view::ViewDepthTexture>();
    ASSERT_TRUE(plain_depth);
    ASSERT_TRUE(prepass_depth);
    // The two views share one texture because they share an output target and
    // sample count, so the union of their requests carries COPY_SRC.
    EXPECT_EQ(plain_depth->get().texture, prepass_depth->get().texture);
    EXPECT_TRUE(static_cast<bool>(static_cast<wgpu::TextureUsage>(
        static_cast<std::uint32_t>(plain_depth->get().texture.getUsage()) &
        static_cast<std::uint32_t>(wgpu::TextureUsage::eCopySrc))));
}

// C15: Bevy's deferred G-buffer phase items are binned, share the lightmap-free
// key pair, and carry Bevy's deferred texture formats
// (`deferred/mod.rs:17-19, 27, 110`).
TEST(Core3dDeferredPhases, MatchBevyBinnedItemContractsAndFormats) {
    using namespace epix::core_graph::core_3d;
    static_assert(phase::BinnedPhaseItem<Opaque3dDeferred>);
    static_assert(phase::BinnedPhaseItem<AlphaMask3dDeferred>);
    static_assert(phase::CachedRenderPipelinePhaseItem<Opaque3dDeferred>);
    static_assert(phase::CachedRenderPipelinePhaseItem<AlphaMask3dDeferred>);
    static_assert(std::same_as<Opaque3dDeferred::BatchSetKey, OpaqueNoLightmap3dBatchSetKey>);
    static_assert(std::same_as<Opaque3dDeferred::BinKey, OpaqueNoLightmap3dBinKey>);
    static_assert(std::same_as<AlphaMask3dDeferred::BatchSetKey, OpaqueNoLightmap3dBatchSetKey>);
    static_assert(std::same_as<AlphaMask3dDeferred::BinKey, OpaqueNoLightmap3dBinKey>);

    EXPECT_EQ(epix::core_graph::core_3d::DEFERRED_PREPASS_FORMAT, wgpu::TextureFormat::eRGBA32Uint);
    EXPECT_EQ(epix::core_graph::core_3d::DEFERRED_LIGHTING_PASS_ID_FORMAT, wgpu::TextureFormat::eR8Uint);
    EXPECT_EQ(epix::core_graph::core_3d::DEFERRED_LIGHTING_PASS_ID_DEPTH_FORMAT, wgpu::TextureFormat::eDepth16Unorm);
}

// C15: only a camera that carries `DeferredPrepass` gets the deferred phases, and
// a deferred view receives Bevy's two deferred textures with their formats at a
// fixed single sample count (`core_3d/mod.rs:731-745` and `:1134-1198`).
TEST(Core3dDeferredTextures, RequiresDeferredPrepassAndAllocatesBevyFormats) {
    using namespace epix::core_graph::core_3d;
    epix::app::App app = epix::app::App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::app::TaskPoolPlugin{})
        .add_plugins(epix::time::TimePlugin{})
        .add_plugins(epix::camera::CameraPlugin{})
        .add_plugins(epix::assets::AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{});
    try {
        app.add_plugins(RenderPlugin{});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan unavailable: " << error.what();
    }
    auto& render_app = app.get_sub_app_mut(Render)->get();
    auto& world      = render_app.world_mut();
    init_core3d_test_resources(world);
    world.init_resource<batching::GpuPreprocessingSupport>();
    world.init_resource<phase::ViewBinnedRenderPhases<Opaque3dPrepass>>();
    world.init_resource<phase::ViewBinnedRenderPhases<AlphaMask3dPrepass>>();
    world.init_resource<phase::ViewBinnedRenderPhases<Opaque3dDeferred>>();
    world.init_resource<phase::ViewBinnedRenderPhases<AlphaMask3dDeferred>>();
    render_app.add_systems(epix::app::Update, into(prepare_prepass_textures));

    const ::epix::camera::NormalizedRenderTarget target{::epix::camera::ManualTextureViewHandle{101}};
    const auto spawn = [&](bool deferred_prepass, view::Msaa msaa) {
        auto entity         = world.spawn();
        const auto id       = entity.id();
        const auto retained = view::RetainedViewEntity::create(sync_world::MainEntity{id}, std::nullopt, 0);
        entity.insert(camera::ExtractedCamera{.target = target, .physical_target_size = glm::uvec2{32, 24}},
                      view::ExtractedView{.retained_view_entity = retained}, msaa);
        // A deferred view still belongs to both prepass phases, plus both
        // deferred phases when it carries the component.
        world.resource_mut<phase::ViewBinnedRenderPhases<Opaque3dPrepass>>().prepare_for_new_frame(
            retained, batching::GpuPreprocessingMode::None);
        world.resource_mut<phase::ViewBinnedRenderPhases<AlphaMask3dPrepass>>().prepare_for_new_frame(
            retained, batching::GpuPreprocessingMode::None);
        if (deferred_prepass) {
            entity.insert(DeferredPrepass{});
            world.resource_mut<phase::ViewBinnedRenderPhases<Opaque3dDeferred>>().prepare_for_new_frame(
                retained, batching::GpuPreprocessingMode::None);
            world.resource_mut<phase::ViewBinnedRenderPhases<AlphaMask3dDeferred>>().prepare_for_new_frame(
                retained, batching::GpuPreprocessingMode::None);
        }
        return Core3dTestView{.entity = id, .retained = retained};
    };

    // A deferred view on an MSAA target still gets single-sampled deferred
    // textures, which is Bevy's documented rule for the resolved G-buffer.
    const auto deferred = spawn(true, view::Msaa::Sample4);
    const auto plain    = spawn(false, view::Msaa::Off);

    render_app.run_schedule(epix::app::Update);

    const auto deferred_textures = world.entity(deferred.entity).get<ViewPrepassTextures>();
    ASSERT_TRUE(deferred_textures);
    ASSERT_TRUE(deferred_textures->get().deferred);
    ASSERT_TRUE(deferred_textures->get().deferred_lighting_pass_id);
    EXPECT_EQ(deferred_textures->get().deferred->texture.texture.getFormat(), DEFERRED_PREPASS_FORMAT);
    EXPECT_EQ(deferred_textures->get().deferred_lighting_pass_id->texture.texture.getFormat(),
              DEFERRED_LIGHTING_PASS_ID_FORMAT);
    // Sampleable render attachments, both single-sampled.
    EXPECT_EQ(deferred_textures->get().deferred->texture.texture.getUsage(),
              wgpu::TextureUsage::eRenderAttachment | wgpu::TextureUsage::eTextureBinding);
    EXPECT_EQ(deferred_textures->get().deferred->texture.texture.getSampleCount(), 1u);
    EXPECT_EQ(deferred_textures->get().deferred_lighting_pass_id->texture.texture.getSampleCount(), 1u);
    EXPECT_TRUE(deferred_textures->get().deferred_view().has_value());

    // A view without `DeferredPrepass` allocates neither deferred texture.
    const auto plain_textures = world.entity(plain.entity).get<ViewPrepassTextures>();
    ASSERT_TRUE(plain_textures);
    EXPECT_FALSE(plain_textures->get().deferred);
    EXPECT_FALSE(plain_textures->get().deferred_lighting_pass_id);
    EXPECT_FALSE(plain_textures->get().deferred_view().has_value());
}

// C15-D4: `prepare_deferred_lighting_id_textures` gives every view with a
// `DeferredPrepass` one single-sampled `eDepth16Unorm` texture usable as a render
// attachment and as a copy source, and gives a view without the component none
// (`deferred/copy_lighting_id.rs:160-193`).
TEST(Core3dDeferredTextures, CopyLightingIdAllocatesADepthTargetPerDeferredView) {
    using namespace epix::core_graph::core_3d;
    epix::app::App app = epix::app::App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::app::TaskPoolPlugin{})
        .add_plugins(epix::time::TimePlugin{})
        .add_plugins(epix::camera::CameraPlugin{})
        .add_plugins(epix::assets::AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{});
    try {
        app.add_plugins(RenderPlugin{});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan unavailable: " << error.what();
    }
    auto& render_app = app.get_sub_app_mut(Render)->get();
    auto& world      = render_app.world_mut();
    init_core3d_test_resources(world);
    render_app.add_systems(epix::app::Update, into(prepare_deferred_lighting_id_textures));

    const ::epix::camera::NormalizedRenderTarget target{::epix::camera::ManualTextureViewHandle{102}};
    const auto spawn = [&](bool deferred_prepass, std::optional<glm::uvec2> size) {
        auto entity = world.spawn();
        const auto id = entity.id();
        entity.insert(camera::ExtractedCamera{.target = target, .physical_target_size = size});
        if (deferred_prepass) entity.insert(DeferredPrepass{});
        return id;
    };
    const auto deferred = spawn(true, glm::uvec2{48, 32});
    const auto plain    = spawn(false, glm::uvec2{48, 32});
    // A deferred view with no physical target size is skipped rather than
    // allocated at a zero extent.
    const auto no_size = spawn(true, std::nullopt);

    render_app.run_schedule(epix::app::Update);

    const auto lighting_id = world.entity(deferred).get<DeferredLightingIdDepthTexture>();
    ASSERT_TRUE(lighting_id);
    const auto& texture = lighting_id->get().texture.texture;
    EXPECT_EQ(texture.getFormat(), DEFERRED_LIGHTING_PASS_ID_DEPTH_FORMAT);
    EXPECT_EQ(texture.getSampleCount(), 1u);
    EXPECT_EQ(texture.getWidth(), 48u);
    EXPECT_EQ(texture.getHeight(), 32u);
    EXPECT_EQ(texture.getUsage(),
              wgpu::TextureUsage::eRenderAttachment | wgpu::TextureUsage::eCopySrc);
    EXPECT_TRUE(static_cast<bool>(lighting_id->get().texture.default_view));

    EXPECT_FALSE(world.entity(plain).contains<DeferredLightingIdDepthTexture>());
    EXPECT_FALSE(world.entity(no_size).contains<DeferredLightingIdDepthTexture>());
}
