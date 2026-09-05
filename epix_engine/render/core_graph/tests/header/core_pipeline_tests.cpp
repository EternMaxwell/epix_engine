#include <gtest/gtest.h>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <epix/time.hpp>
#include <array>
#include <ranges>
#include <type_traits>

using namespace epix::ecs;
using namespace epix::render;

// C3 FullscreenMaterial probe: a uniform-only material that satisfies the
// `FullscreenMaterial` concept. Its directive specializations must be defined
// before its static methods reference the material's own label.
struct C3FullscreenMaterialProbe {
    float value = 1.0f;

    static std::string_view fragment_shader();
    static std::string_view fragment_shader_source();
    static std::vector<::epix::render::graph::NodeLabel> node_edges();
    static std::optional<::epix::render::graph::GraphLabel> sub_graph();
};

template <>
struct epix::render::ExtractComponent<C3FullscreenMaterialProbe> {
    using QueryData   = const C3FullscreenMaterialProbe&;
    using QueryFilter = ::epix::ecs::Filter<>;
    using Out         = C3FullscreenMaterialProbe;

    static std::optional<Out> extract_component(QueryData value) { return value; }
};

namespace epix::render::render_resource {
template <>
struct ShaderTypeInfo<C3FullscreenMaterialProbe> : RawShaderType<C3FullscreenMaterialProbe> {};
}  // namespace epix::render::render_resource

std::string_view C3FullscreenMaterialProbe::fragment_shader() { return "core_pipeline/fullscreen_probe.slang"; }

std::string_view C3FullscreenMaterialProbe::fragment_shader_source() { return "void fullscreen_probe_dummy() {}\n"; }

std::vector<::epix::render::graph::NodeLabel> C3FullscreenMaterialProbe::node_edges() {
    return {::epix::render::graph::NodeLabel{::epix::core_graph::core_2d::Core2dNodes::Tonemapping},
            ::epix::core_graph::fullscreen_material_node_label<C3FullscreenMaterialProbe>(),
            ::epix::render::graph::NodeLabel{::epix::core_graph::core_2d::Core2dNodes::EndMainPassPostProcessing}};
}

std::optional<::epix::render::graph::GraphLabel> C3FullscreenMaterialProbe::sub_graph() {
    return ::epix::render::graph::GraphLabel{::epix::core_graph::core_2d::Core2d};
}

// Bevy core_pipeline::FullscreenShader provides the common generated
// fullscreen-triangle state: the vertex stage has no user vertex buffers and
// uses the canonical entry point.
TEST(FullscreenShader, BuildsGeneratedFullscreenTriangleState) {
    const auto handle = epix::assets::Handle<epix::shader::Shader>{
        epix::assets::AssetId<epix::shader::Shader>::invalid()};
    const epix::core_graph::FullscreenShader fullscreen{handle};

    const auto state = fullscreen.to_vertex_state();
    EXPECT_EQ(fullscreen.shader(), handle);
    EXPECT_EQ(state.shader, handle);
    EXPECT_TRUE(state.shader_defs.empty());
    EXPECT_TRUE(state.buffers.empty());
    ASSERT_TRUE(state.entry_point.has_value());
    EXPECT_EQ(*state.entry_point, "fullscreen_vertex_shader");
}

// Bevy core_pipeline::BlitPipeline specializes a reusable fullscreen texture
// copy by target format, blend state, and sample count.
TEST(BlitPipeline, SpecializesFormatBlendAndSamples) {
    const auto handle = epix::assets::Handle<epix::shader::Shader>{
        epix::assets::AssetId<epix::shader::Shader>::invalid()};
    const epix::core_graph::BlitPipeline pipeline{
        .layout            = {},
        .sampler           = {},
        .fullscreen_shader = epix::core_graph::FullscreenShader{handle},
        .fragment_shader   = handle,
    };
    const auto alpha = wgpu::BlendState()
                           .setColor(wgpu::BlendComponent()
                                         .setOperation(wgpu::BlendOperation::eAdd)
                                         .setSrcFactor(wgpu::BlendFactor::eSrcAlpha)
                                         .setDstFactor(wgpu::BlendFactor::eOneMinusSrcAlpha))
                           .setAlpha(wgpu::BlendComponent()
                                         .setOperation(wgpu::BlendOperation::eAdd)
                                         .setSrcFactor(wgpu::BlendFactor::eOne)
                                         .setDstFactor(wgpu::BlendFactor::eOneMinusSrcAlpha));
    const epix::core_graph::BlitPipelineKey key{.texture_format = wgpu::TextureFormat::eBGRA8Unorm,
                                                 .blend_state    = alpha,
                                                 .samples        = 4};
    const epix::core_graph::BlitPipelineKey equivalent = key;
    const epix::core_graph::BlitPipelineKey different_samples{.texture_format = wgpu::TextureFormat::eBGRA8Unorm,
                                                               .blend_state    = alpha,
                                                               .samples        = 1};
    static_assert(epix::render::SpecializedRenderPipeline<epix::core_graph::BlitPipeline>);
    EXPECT_EQ(key, equivalent);
    EXPECT_NE(key, different_samples);
    EXPECT_EQ(std::hash<epix::core_graph::BlitPipelineKey>{}(key),
              std::hash<epix::core_graph::BlitPipelineKey>{}(equivalent));

    const auto descriptor = pipeline.specialize(key);
    EXPECT_EQ(descriptor.layouts.size(), 1u);
    EXPECT_EQ(descriptor.vertex.shader, handle);
    ASSERT_TRUE(descriptor.fragment.has_value());
    ASSERT_EQ(descriptor.fragment->targets.size(), 1u);
    EXPECT_EQ(descriptor.fragment->targets.front().format, key.texture_format);
    EXPECT_EQ(descriptor.multisample.count, key.samples);
    EXPECT_EQ(descriptor.multisample.mask, ~0u);
    EXPECT_EQ(descriptor.primitive.frontFace, wgpu::FrontFace::eCCW);
    EXPECT_EQ(descriptor.primitive.cullMode, wgpu::CullMode::eNone);
    ASSERT_TRUE(descriptor.fragment->targets.front().blend.has_value());
    EXPECT_EQ(descriptor.fragment->targets.front().blend->color.srcFactor, wgpu::BlendFactor::eSrcAlpha);
}

// Bevy's UpscalingPlugin owns the per-view selected pipeline and installs a
// typed view node in Core2D.  This verifies the public registration contract;
// the GLFW mesh-rendering example verifies the resulting output path.
TEST(UpscalingPlugin, RegistersPerViewPipelineAndTypedCore2dNode) {
    static_assert(epix::render::graph::ViewNode<epix::core_graph::UpscalingNode>);
    static_assert(!std::copy_constructible<PipelineServer>);
    static_assert(std::movable<PipelineServer>);
    static_assert(std::ranges::view<decltype(std::declval<const PipelineServer&>().waiting_pipelines())>);

    epix::app::App app = epix::app::App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::app::TaskPoolPlugin{})
        .add_plugins(epix::time::TimePlugin{})
        .add_plugins(epix::camera::CameraPlugin{})
        .add_plugins(epix::assets::AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{});
    try {
        RenderPlugin{}.attach(app);
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan unavailable: " << error.what();
    }
    app.add_plugins(epix::core_graph::CoreGraphPlugin{});

    auto render_app = app.get_sub_app_mut(Render);
    ASSERT_TRUE(render_app.has_value());
    const auto main_pipeline_server = app.world().get_resource<PipelineServer>();
    const auto render_pipeline_server = render_app->get().world().get_resource<PipelineServer>();
    ASSERT_TRUE(main_pipeline_server.has_value());
    ASSERT_TRUE(render_pipeline_server.has_value());
    EXPECT_NE(std::addressof(main_pipeline_server->get()), std::addressof(render_pipeline_server->get()));
    auto& render_instance = render_app->get();
    render_instance.run_schedule(RenderStartup);
    auto& world = render_instance.world_mut();
    EXPECT_TRUE(world.get_resource<epix::render::SpecializedRenderPipelines<epix::core_graph::BlitPipeline>>().has_value());

    // Bevy calls specialize from prepare_view_upscaling_pipelines every
    // Prepare frame. The cache must therefore return the existing ID rather
    // than queueing a new render pipeline for the same output key.
    const auto pipeline_server = world.get_resource<PipelineServer>();
    const auto blit_pipeline   = world.get_resource<epix::core_graph::BlitPipeline>();
    auto specialized_pipelines = world.get_resource_mut<epix::render::SpecializedRenderPipelines<epix::core_graph::BlitPipeline>>();
    ASSERT_TRUE(pipeline_server.has_value());
    ASSERT_TRUE(blit_pipeline.has_value());
    ASSERT_TRUE(specialized_pipelines.has_value());
    const epix::core_graph::BlitPipelineKey key{.texture_format = wgpu::TextureFormat::eBGRA8Unorm, .samples = 1};
    const auto first = specialized_pipelines->get().specialize(pipeline_server->get(), blit_pipeline->get(), key);
    const auto again = specialized_pipelines->get().specialize(pipeline_server->get(), blit_pipeline->get(), key);
    EXPECT_EQ(first, again);
    EXPECT_EQ(specialized_pipelines->get().cache.size(), 1u);

    const auto graph = world.get_resource<epix::render::graph::RenderGraph>();
    ASSERT_TRUE(graph.has_value());
    const auto core2d = graph->get().get_sub_graph(epix::core_graph::core_2d::Core2d);
    ASSERT_TRUE(core2d.has_value());
    EXPECT_TRUE(core2d->get().get_node_state(epix::core_graph::core_2d::Core2dNodes::Upscaling).has_value());
}

// Bevy Core2dPlugin supplies these three Camera2d requirements and extracts
// the marker into the render world for its Core2D-only queries.
TEST(Core2dPlugin, AddsCameraRequirementsAndExtractsCamera2d) {
    epix::app::App app = epix::app::App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::app::TaskPoolPlugin{})
        .add_plugins(epix::time::TimePlugin{})
        .add_plugins(epix::render::FrameCountPlugin{})
        .add_plugins(epix::camera::CameraPlugin{})
        .add_plugins(epix::assets::AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{});
    try {
        RenderPlugin{}.attach(app);
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan unavailable: " << error.what();
    }
    app.add_plugins(epix::core_graph::core_2d::Core2dPlugin{});
    ASSERT_TRUE(app.world().get_resource<FrameCount>().has_value());

    const Entity camera = app.world_mut().spawn(::epix::camera::Camera2d{}).id();
    const auto main = app.world().get_entity(camera);
    ASSERT_TRUE(main.has_value());
    EXPECT_TRUE(main->contains<::epix::camera::Camera>());
    EXPECT_TRUE(main->contains<epix::core_graph::DebandDither>());
    EXPECT_TRUE(main->contains<epix::core_graph::Tonemapping>());
    EXPECT_TRUE(main->contains<epix::render::camera::CameraRenderGraph>());
    EXPECT_EQ(main->get<epix::core_graph::DebandDither>()->get(),
              epix::core_graph::DebandDither::Disabled);
    EXPECT_EQ(main->get<epix::core_graph::Tonemapping>()->get(), epix::core_graph::Tonemapping::None);
    EXPECT_EQ(main->get<epix::render::camera::CameraRenderGraph>()->get(),
              epix::render::graph::GraphLabel(epix::core_graph::core_2d::Core2d));

    // Phase ownership follows Camera2d, not the chosen graph. Custom graphs
    // can reuse these phases, and a missing graph must not suppress extraction.
    const Entity custom_graph = app.world_mut().spawn(
        ::epix::camera::Camera2d{},
        epix::render::camera::CameraRenderGraph{epix::core_graph::core_2d::Core2dNodes::Bloom}).id();
    const Entity no_graph = app.world_mut().spawn(::epix::camera::Camera2d{}).id();
    app.world_mut().entity_mut(no_graph).remove<epix::render::camera::CameraRenderGraph>();
    ::epix::camera::Camera inactive_camera;
    inactive_camera.is_active = false;
    const Entity inactive = app.world_mut().spawn(::epix::camera::Camera2d{}, inactive_camera).id();
    const Entity non_2d = app.world_mut().spawn(::epix::camera::Camera{}).id();

    auto render_sub = app.take_sub_app(Render);
    ASSERT_TRUE(render_sub);
    render_sub->extract(app);
    // RenderPlugin deliberately defers ExtractSchedule commands until the
    // RenderSystems::ExtractCommands stage. Apply that stage's source
    // schedule here rather than assuming extraction inserts immediately.
    render_sub->resource_scope([](epix::ecs::Schedules& schedules, epix::ecs::World& world) {
        schedules.schedule_mut(ExtractSchedule).apply_deferred(world);
    });
    const auto render_entity = app.world()
                                   .get_entity(camera)
                                   .and_then([](const EntityRef& entity) { return entity.get<sync_world::RenderEntity>(); })
                                   .transform([](const auto& value) { return value.get().id(); });
    ASSERT_TRUE(render_entity.has_value());
    const auto extracted = render_sub->world().get_entity(*render_entity);
    ASSERT_TRUE(extracted.has_value());
    EXPECT_TRUE(extracted->contains<::epix::camera::Camera2d>());
    const auto check_phases = [&](Entity entity, bool expected) {
        const auto retained = view::RetainedViewEntity::create(sync_world::MainEntity{entity}, std::nullopt, 0);
        using namespace epix::core_graph::core_2d;
        EXPECT_EQ(render_sub->resource<phase::ViewSortedRenderPhases<Transparent2D>>().contains(retained), expected);
        EXPECT_EQ(render_sub->resource<phase::ViewBinnedRenderPhases<Opaque2D>>().phases.contains(retained), expected);
        EXPECT_EQ(render_sub->resource<phase::ViewBinnedRenderPhases<AlphaMask2D>>().phases.contains(retained), expected);
    };
    for (Entity entity : {camera, custom_graph, no_graph}) check_phases(entity, true);
    for (Entity entity : {inactive, non_2d}) check_phases(entity, false);

    app.world_mut().entity_mut(custom_graph).insert(inactive_camera);
    app.world_mut().entity_mut(no_graph).despawn();
    render_sub->extract(app);
    check_phases(camera, true);
    check_phases(custom_graph, false);
    check_phases(no_graph, false);
    app.insert_sub_app(Render, std::move(render_sub));
}

TEST(Core2dDepthTextures, FiltersCameraMarkerAndPhasesAndSharesPerTarget) {
    using namespace epix::core_graph::core_2d;
    static_assert(CORE_2D_DEPTH_FORMAT == wgpu::TextureFormat::eDepth32Float);
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
    auto& world = render_app.world_mut();
    world.init_resource<phase::ViewSortedRenderPhases<Transparent2D>>();
    world.init_resource<phase::ViewBinnedRenderPhases<Opaque2D>>();
    render_app.add_systems(epix::app::Update, into(prepare_core_2d_depth_textures));

    const auto spawn_view = [&](bool camera2d, bool opaque, bool transparent,
                                std::optional<::epix::camera::NormalizedRenderTarget> target,
                                std::optional<glm::uvec2> size, view::Msaa msaa) {
        auto entity = world.spawn();
        const auto retained = view::RetainedViewEntity::create(sync_world::MainEntity{entity.id()}, std::nullopt, 0);
        entity.insert(camera::ExtractedCamera{.target = target, .physical_target_size = size},
                      view::ExtractedView{.retained_view_entity = retained}, msaa);
        if (camera2d) entity.insert(::epix::camera::Camera2d{});
        if (opaque) world.resource_mut<phase::ViewBinnedRenderPhases<Opaque2D>>().prepare_for_new_frame(
            retained, batching::GpuPreprocessingMode::None);
        if (transparent) world.resource_mut<phase::ViewSortedRenderPhases<Transparent2D>>().insert_or_clear(retained);
        return entity.id();
    };
    const ::epix::camera::NormalizedRenderTarget target{::epix::camera::ManualTextureViewHandle{42}};
    const auto first = spawn_view(true, true, true, target, glm::uvec2{64, 32}, view::Msaa::Sample4);
    const auto shared = spawn_view(true, true, true, target, glm::uvec2{64, 32}, view::Msaa::Sample4);
    const auto no_marker = spawn_view(false, true, true, target, glm::uvec2{64, 32}, view::Msaa::Sample4);
    const auto no_opaque = spawn_view(true, false, true, target, glm::uvec2{64, 32}, view::Msaa::Sample4);
    const auto no_transparent = spawn_view(true, true, false, target, glm::uvec2{64, 32}, view::Msaa::Sample4);
    const auto no_size = spawn_view(true, true, true, target, std::nullopt, view::Msaa::Sample4);
    // Bevy keys the per-frame cache by Option<NormalizedRenderTarget>: None
    // is a valid cache key when a physical target size is available.
    const auto no_target = spawn_view(true, true, true, std::nullopt, glm::uvec2{16, 8}, view::Msaa::Off);
    const auto shared_none = spawn_view(true, true, true, std::nullopt, glm::uvec2{16, 8}, view::Msaa::Off);
    for (int frame = 0; frame < 2; ++frame) {
        render_app.run_schedule(epix::app::Update);
        for (Entity entity : {no_marker, no_opaque, no_transparent, no_size}) {
            EXPECT_FALSE(world.entity(entity).contains<view::ViewDepthTexture>());
        }
        const auto depth = world.entity(first).get<view::ViewDepthTexture>();
        const auto shared_depth = world.entity(shared).get<view::ViewDepthTexture>();
        const auto none_depth = world.entity(no_target).get<view::ViewDepthTexture>();
        const auto shared_none_depth = world.entity(shared_none).get<view::ViewDepthTexture>();
        ASSERT_TRUE(depth);
        ASSERT_TRUE(shared_depth);
        ASSERT_TRUE(none_depth);
        ASSERT_TRUE(shared_none_depth);
        EXPECT_EQ(depth->get().texture, shared_depth->get().texture);
        EXPECT_EQ(none_depth->get().texture, shared_none_depth->get().texture);
        EXPECT_NE(depth->get().texture, none_depth->get().texture);
        EXPECT_EQ(depth->get().texture.getWidth(), 64u);
        EXPECT_EQ(depth->get().texture.getHeight(), 32u);
        EXPECT_EQ(depth->get().texture.getSampleCount(), 4u);
        EXPECT_EQ(depth->get().texture.getFormat(), wgpu::TextureFormat::eDepth32Float);
        EXPECT_EQ(depth->get().texture.getUsage(), wgpu::TextureUsage::eRenderAttachment);
        EXPECT_EQ(none_depth->get().texture.getSampleCount(), 1u);
        const auto attachment = depth->get().get_attachment(wgpu::StoreOp::eStore);
        EXPECT_EQ(attachment.depthLoadOp, wgpu::LoadOp::eClear);
        EXPECT_EQ(attachment.depthClearValue, 0.0f);
        EXPECT_EQ(depth->get().get_attachment(wgpu::StoreOp::eStore).depthLoadOp, wgpu::LoadOp::eLoad);
    }
}

// Bevy core_2d::graph exposes a complete label set, while Core2dPlugin owns
// only the default main-pass and post-processing endpoints. Optional plugins
// add their own labeled nodes later.
TEST(Core2dGraph, MatchesBevyDefaultNodesAndChain) {
    epix::ecs::World world(1);
    epix::render::graph::RenderGraph root_graph;
    epix::core_graph::core_2d::Core2d.add_to(root_graph, world);
    const auto core2d = root_graph.get_sub_graph(epix::core_graph::core_2d::Core2d);
    ASSERT_TRUE(core2d.has_value());

    using Node = epix::core_graph::core_2d::Core2dNodes;
    for (const auto node : {Node::StartMainPass, Node::MainOpaquePass, Node::MainTransparentPass,
                            Node::EndMainPass, Node::StartMainPassPostProcessing, Node::Tonemapping,
                            Node::EndMainPassPostProcessing, Node::Upscaling}) {
        EXPECT_TRUE(core2d->get().get_node_state(node).has_value());
    }
    for (const auto node : {Node::MsaaWriteback, Node::Wireframe, Node::Bloom, Node::PostProcessing, Node::Fxaa,
                            Node::Smaa, Node::ContrastAdaptiveSharpening}) {
        EXPECT_FALSE(core2d->get().get_node_state(node).has_value());
    }

    EXPECT_TRUE(core2d->get().has_edge(graph::Edge::node_edge(Node::StartMainPass, Node::MainOpaquePass)));
    EXPECT_TRUE(core2d->get().has_edge(graph::Edge::node_edge(Node::MainOpaquePass, Node::MainTransparentPass)));
    EXPECT_TRUE(core2d->get().has_edge(graph::Edge::node_edge(Node::MainTransparentPass, Node::EndMainPass)));
    EXPECT_TRUE(core2d->get().has_edge(
        graph::Edge::node_edge(Node::EndMainPass, Node::StartMainPassPostProcessing)));
    EXPECT_TRUE(core2d->get().has_edge(
        graph::Edge::node_edge(Node::StartMainPassPostProcessing, Node::Tonemapping)));
    EXPECT_TRUE(core2d->get().has_edge(
        graph::Edge::node_edge(Node::Tonemapping, Node::EndMainPassPostProcessing)));
    EXPECT_TRUE(core2d->get().has_edge(graph::Edge::node_edge(Node::EndMainPassPostProcessing, Node::Upscaling)));
}

TEST(Transparent2D, CarriesIndexedDrawDiscriminator) {
    static_assert(phase::SortedPhaseItem<epix::core_graph::core_2d::Transparent2D>);
    const auto entity = Entity::from_index(1);
    epix::core_graph::core_2d::Transparent2D indexed{
        .representative_entity = {entity, sync_world::MainEntity{entity}}, .indexed_value = true};
    epix::core_graph::core_2d::Transparent2D non_indexed{
        .representative_entity = {entity, sync_world::MainEntity{entity}}, .indexed_value = false};
    EXPECT_TRUE(indexed.indexed());
    EXPECT_FALSE(non_indexed.indexed());
}

TEST(Core2dBinnedPhases, MatchBevyOpaqueAndAlphaMaskKeys) {
    using namespace epix::core_graph::core_2d;
    static_assert(phase::BinnedPhaseItem<Opaque2D>);
    static_assert(phase::BinnedPhaseItem<AlphaMask2D>);

    const auto entity = Entity::from_index(92);
    const auto asset = epix::assets::UntypedAssetId(epix::assets::AssetId<std::uint32_t>::invalid());
    const BatchSetKey2D indexed{.indexed_value = true};
    EXPECT_TRUE(indexed.indexed());

    const Opaque2DBinKey opaque_key{.pipeline_id = CachedPipelineId{4},
                                    .draw_func = phase::DrawFunctionId{2},
                                    .asset_id = asset};
    const auto opaque_extra = phase::PhaseItemExtraIndex::dynamic_offset(12);
    const auto opaque = Opaque2D::create(indexed, opaque_key, {entity, sync_world::MainEntity{entity}}, {7, 9},
                                         opaque_extra);
    EXPECT_EQ(opaque.cached_pipeline(), CachedPipelineId{4});
    EXPECT_EQ(opaque.draw_function(), phase::DrawFunctionId{2});
    EXPECT_EQ(opaque.batch_range(), (std::pair<std::uint32_t, std::uint32_t>{7, 9}));
    EXPECT_EQ(opaque.extra_index(), opaque_extra);

    const AlphaMask2DBinKey mask_key{.pipeline_id = CachedPipelineId{5},
                                      .draw_func = phase::DrawFunctionId{3},
                                      .asset_id = asset};
    const auto mask = AlphaMask2D::create(indexed, mask_key, {entity, sync_world::MainEntity{entity}}, {1, 2},
                                           phase::PhaseItemExtraIndex::None);
    EXPECT_EQ(mask.cached_pipeline(), CachedPipelineId{5});
    EXPECT_EQ(mask.draw_function(), phase::DrawFunctionId{3});
}

TEST(FullscreenMaterial, StructuredForBevyParity) {
    // C3: the FullscreenMaterial concept is satisfied by a uniform component,
    // the material label is a dedicated NodeLabel subtype, and the typed view
    // node runs through a ViewNodeRunner. Attaching is safe without a render
    // sub-app (the plugin family contract).
    static_assert(::epix::core_graph::FullscreenMaterial<C3FullscreenMaterialProbe>);
    static_assert(std::is_base_of_v<::epix::render::graph::NodeLabel, ::epix::core_graph::FullscreenMaterialLabel>);
    static_assert(std::is_base_of_v<::epix::render::graph::Node,
                                    ::epix::core_graph::FullscreenMaterialNodeRunner<C3FullscreenMaterialProbe>>);
    static_assert(::epix::ecs::readonly_query_data<
                  typename ::epix::core_graph::FullscreenMaterialNode<C3FullscreenMaterialProbe>::ViewQuery>);

    auto app = epix::app::App::create();
    EXPECT_NO_THROW(::epix::core_graph::FullscreenMaterialPlugin<C3FullscreenMaterialProbe>{}.attach(app));
}
