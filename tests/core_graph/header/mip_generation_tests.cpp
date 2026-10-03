#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph/mip_generation.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/window.hpp>
#include <gtest/gtest.h>

using namespace epix::core_graph::mip_generation;

namespace {

/** @brief Asset loading spawns tasks on the IO pool, so a test that loads any
 *  asset needs it initialized (mirrors `render_smoke_test.cpp`). */
struct MipGenerationTaskPoolInit {
    MipGenerationTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPool{epix::task::TaskPoolBuilder{}.num_threads(4).build()});
    }
} g_mip_generation_task_pool_init;

/** @brief The assets/time/camera/image prerequisites a render-world test needs,
 *  mirroring `render_smoke_test.cpp`'s `add_render_test_prerequisites`. */
void add_core_graph_test_prerequisites(epix::app::App& app) {
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(epix::camera::CameraPlugin{})
        .add_plugins(epix::assets::AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::render::FrameCountPlugin{});
}

/** @brief The pyramid's texture is half the depth buffer, rounded up (Bevy
 *  `ViewDepthPyramid::new`, `experimental/mip_generation/mod.rs:590-594`). */
TEST(MipGeneration, PyramidSizeIsHalfTheDepthBufferRoundedUp) {
    EXPECT_EQ(depth_pyramid_size(glm::uvec2(1920u, 1080u)), glm::uvec2(960u, 540u));
    EXPECT_EQ(depth_pyramid_size(glm::uvec2(1u, 1u)), glm::uvec2(1u, 1u));
    EXPECT_EQ(depth_pyramid_size(glm::uvec2(3u, 2u)), glm::uvec2(2u, 1u));
    EXPECT_EQ(depth_pyramid_size(glm::uvec2(0u, 5u)), glm::uvec2(0u, 3u));
}

/** @brief The mip count is Bevy's `Extent3d::max_mips(D2)`: the base-2
 *  logarithm of the greatest dimension, rounded up, never less than one. */
TEST(MipGeneration, MaxMipsMatchesBevyExtentMath) {
    EXPECT_EQ(max_mips_for_size(glm::uvec2(1u, 1u)), 1u);
    EXPECT_EQ(max_mips_for_size(glm::uvec2(2u, 2u)), 2u);
    EXPECT_EQ(max_mips_for_size(glm::uvec2(4u, 4u)), 3u);
    EXPECT_EQ(max_mips_for_size(glm::uvec2(8u, 1u)), 4u);
    EXPECT_EQ(max_mips_for_size(glm::uvec2(0u, 0u)), 1u);
    EXPECT_EQ(max_mips_for_size(glm::uvec2(2048u, 2048u)), 12u);
}

/** @brief A 4096x4096 depth buffer is the largest the pyramid supports: its
 *  half-size texture needs exactly `DEPTH_PYRAMID_MIP_COUNT` levels. */
TEST(MipGeneration, DepthPyramidMipCountSaturatesAtTheSupportedSize) {
    EXPECT_EQ(depth_pyramid_mip_count(glm::uvec2(4096u, 4096u)), DEPTH_PYRAMID_MIP_COUNT);
    EXPECT_EQ(depth_pyramid_mip_count(glm::uvec2(1920u, 1080u)), 10u);
    EXPECT_EQ(depth_pyramid_mip_count(glm::uvec2(1u, 1u)), 1u);
    EXPECT_EQ(depth_pyramid_mip_count(glm::uvec2(8192u, 8192u)), 13u);
}

/** @brief The pipeline key's flags are Bevy's `DownsampleDepthPipelineKey`
 *  values, and `contains` is a bit test. */
TEST(MipGeneration, PipelineKeyFlags) {
    EXPECT_EQ(DownsampleDepthPipelineKey::MULTISAMPLE, 1u);
    EXPECT_EQ(DownsampleDepthPipelineKey::SECOND_PHASE, 2u);

    const DownsampleDepthPipelineKey empty{};
    EXPECT_FALSE(empty.contains(DownsampleDepthPipelineKey::MULTISAMPLE));
    EXPECT_FALSE(empty.contains(DownsampleDepthPipelineKey::SECOND_PHASE));

    const DownsampleDepthPipelineKey second{DownsampleDepthPipelineKey::SECOND_PHASE};
    EXPECT_TRUE(second.contains(DownsampleDepthPipelineKey::SECOND_PHASE));
    EXPECT_FALSE(second.contains(DownsampleDepthPipelineKey::MULTISAMPLE));

    const DownsampleDepthPipelineKey both{DownsampleDepthPipelineKey::MULTISAMPLE |
                                          DownsampleDepthPipelineKey::SECOND_PHASE};
    EXPECT_TRUE(both.contains(DownsampleDepthPipelineKey::MULTISAMPLE));
    EXPECT_TRUE(both.contains(DownsampleDepthPipelineKey::SECOND_PHASE));
    EXPECT_EQ(both, (DownsampleDepthPipelineKey{DownsampleDepthPipelineKey::MULTISAMPLE |
                                                DownsampleDepthPipelineKey::SECOND_PHASE}));
}

/** @brief `specialize` builds Bevy's descriptor: the first phase's entry point
 *  unless `SECOND_PHASE` is set, the `MULTISAMPLE` shader def only when the
 *  depth buffer is multisampled, and the `0..4` mip-count push constant
 *  (`mip_generation/mod.rs:473-513`). */
TEST(MipGeneration, SpecializeBuildsBevyDescriptor) {
    // The descriptor only needs a shader handle to fill in; a real one would
    // require a running asset server, so this uses an invalid id, which is what
    // `Handle<A>(AssetId<A>::invalid())` exists for.
    const epix::assets::Handle<epix::shader::Shader> shader{
        epix::assets::AssetId<epix::shader::Shader>::invalid()};

    const DownsampleDepthPipeline pipeline{
        .bind_group_layout = create_downsample_depth_bind_group_layout(false),
        .pipeline_id       = std::nullopt,
        .shader            = shader,
    };

    const auto first = pipeline.specialize(DownsampleDepthPipelineKey{});
    EXPECT_EQ(first.entry_point, std::optional<std::string>{"downsample_depth_first"});
    ASSERT_EQ(first.layouts.size(), 1u);
    ASSERT_EQ(first.push_constant_ranges.size(), 1u);
    EXPECT_EQ(first.push_constant_ranges[0].stages, wgpu::ShaderStage::eCompute);
    EXPECT_EQ(first.push_constant_ranges[0].start, 0u);
    EXPECT_EQ(first.push_constant_ranges[0].end, 4u);
    EXPECT_TRUE(first.shader_defs.empty());

    const auto second = pipeline.specialize(DownsampleDepthPipelineKey{DownsampleDepthPipelineKey::SECOND_PHASE});
    EXPECT_EQ(second.entry_point, std::optional<std::string>{"downsample_depth_second"});
    EXPECT_TRUE(second.shader_defs.empty());

    const auto multisample_second =
        pipeline.specialize(DownsampleDepthPipelineKey{DownsampleDepthPipelineKey::MULTISAMPLE |
                                                       DownsampleDepthPipelineKey::SECOND_PHASE});
    EXPECT_EQ(multisample_second.entry_point, std::optional<std::string>{"downsample_depth_second"});
    ASSERT_EQ(multisample_second.shader_defs.size(), 1u);
    EXPECT_EQ(multisample_second.shader_defs[0].name, std::string("MULTISAMPLE"));

    const auto multisample_first =
        pipeline.specialize(DownsampleDepthPipelineKey{DownsampleDepthPipelineKey::MULTISAMPLE});
    EXPECT_EQ(multisample_first.entry_point, std::optional<std::string>{"downsample_depth_first"});
    ASSERT_EQ(multisample_first.shader_defs.size(), 1u);
    EXPECT_EQ(multisample_first.shader_defs[0].name, std::string("MULTISAMPLE"));
}

/** @brief The layout and the dummy texture are accepted by a real device: the
 *  layout has the depth binding, one storage binding per pyramid mip and the
 *  sampler, and the dummy texture is a one-texel R32Float storage texture
 *  (`mip_generation/mod.rs:420-455`, `:533-560`). */
TEST(MipGeneration, LayoutAndDummyTextureAreValidOnDevice) {
    auto app = epix::app::App::create();
    add_core_graph_test_prerequisites(app);
    try {
        app.add_plugins(epix::render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    const auto render_app = app.get_sub_app(epix::render::Render);
    ASSERT_TRUE(render_app.has_value());
    const auto device = render_app->get().world().resource<wgpu::Device>();
    auto& pipeline_server = app.resource<epix::render::PipelineServer>();

    for (const bool multisampled : {false, true}) {
        const auto raw_layout =
            pipeline_server.get_bind_group_layout(create_downsample_depth_bind_group_layout(multisampled));
        EXPECT_FALSE(!raw_layout);
    }

    const auto dummy = create_depth_pyramid_dummy_texture(device, "depth pyramid dummy texture",
                                                          "depth pyramid dummy texture view");
    EXPECT_FALSE(!dummy);
}

/** @brief The depth-pyramid bindings are expressible in Slang and accepted by
 *  wgpu against the depth-typed layout.
 *
 *  wgpu requires the shader's depth binding to be a depth image, and Slang's
 *  SPIR-V backend does not set the `Depth` operand for a plain `Texture2D<float>`
 *  (gfx-rs/wgpu#7332). Two Slang-specific forms are needed: the built-in
 *  `DepthTexture2D`/`DepthTexture2DMS` types, whose `_Texture` specialization
 *  sets `isShadow` and therefore emits a depth image, and an explicit
 *  `[[vk::image_format("r32f")]]` on every write-only storage image, without
 *  which Slang emits a storage image of unknown format and the module demands
 *  the `StorageImageReadWithoutFormat` capability that wgpu does not support. */
TEST(MipGeneration, DepthTextureBindingCompilesWithSlangDepthType) {
    static constexpr std::string_view kProbeSource = R"slang(
// Slang's built-in depth-texture type. Its `_Texture` specialization sets
// `isShadow`, which is what makes the SPIR-V backend emit a depth image;
// declaring a plain `Texture2D<float>` yields a sampled float image that wgpu
// rejects against the depth-typed layout entry (gfx-rs/wgpu#7332).
[[vk::binding(0, 0)]] DepthTexture2D mip_0;
[[vk::binding(1, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> mip_1;
[[vk::binding(13, 0)]] SamplerState samplr;

struct Constants { uint max_mip_level; };
[[vk::push_constant]] ConstantBuffer<Constants> constants;

[shader("compute")]
[numthreads(1, 1, 1)]
void downsample_depth_first() {
    const float value = mip_0.Load(int3(0, 0, 0));
    mip_1[uint2(0, 0)] = value + float(constants.max_mip_level);
}
)slang";

    auto app = epix::app::App::create();
    add_core_graph_test_prerequisites(app);
    try {
        app.add_plugins(epix::render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    app.resource_mut<epix::assets::EmbeddedAssetRegistry>().insert_asset_static(
        "mip_generation_tests/depth_binding.slang", std::as_bytes(std::span(kProbeSource)));
    const auto shader = app.resource<epix::assets::AssetServer>().load<epix::shader::Shader>(
        "embedded://mip_generation_tests/depth_binding.slang");
    for (int attempt = 0;
         attempt < 1000 && !app.resource<epix::assets::AssetServer>().is_loaded_with_dependencies(shader.id());
         ++attempt) {
        app.run_schedule(epix::app::PreUpdate);
        app.run_schedule(epix::app::PostUpdate);
        app.run_schedule(epix::app::Last);
        std::this_thread::yield();
    }

    const DownsampleDepthPipeline pipeline{
        .bind_group_layout = create_downsample_depth_bind_group_layout(false),
        .pipeline_id       = std::nullopt,
        .shader            = shader,
    };
    const auto id = app.resource<epix::render::PipelineServer>().queue_compute_pipeline(
        pipeline.specialize(DownsampleDepthPipelineKey{}));
    app.run_schedule(epix::app::PreUpdate);
    app.run_schedule(epix::app::PostUpdate);
    app.run_schedule(epix::app::Last);
    EXPECT_TRUE(app.resource<epix::render::PipelineServer>().get_compute_pipeline(id).has_value());
}

/** @brief `MipGenerationPlugin` installs the shader and, when the device
 *  supports culling, specializes Bevy's four downsample pipelines (Bevy
 *  `MipGenerationPlugin::build` + `create_downsample_depth_pipelines`,
 *  `experimental/mip_generation/mod.rs:73-122`, `:334-417`). All four must
 *  compile: both phases for a plain depth buffer and for a multisampled one. */
TEST(MipGeneration, PluginCreatesAllFourDownsamplePipelines) {
    auto app = epix::app::App::create();
    add_core_graph_test_prerequisites(app);
    try {
        app.add_plugins(epix::render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    app.add_plugins(epix::core_graph::mip_generation::MipGenerationPlugin{});

    // `MeshAllocatorPlugin` creates `MeshAllocator` in its `finish`/ready hook,
    // which a fixture that never calls `App::run` has to invoke directly
    // (`bevy_render/src/mesh/allocator.rs:1067-1073`); the render schedule below
    // runs the system that needs it.
    epix::render::mesh::MeshAllocatorPlugin{}.ready(app);

    auto render_app = app.get_sub_app_mut(epix::render::Render);
    ASSERT_TRUE(render_app.has_value());

    // This fixture drives the render schedule without a live frame, so the systems
    // that expect one (frame count, clear color, view attachments) cannot validate
    // their parameters. A failed system now stops the schedule, as a system error
    // does in Bevy; this headless harness opts into skipping them for this run
    // instead. The assertions below cover the system under test either way.
    render_app->get().schedule_scope(
        epix::render::Render, [](epix::ecs::Schedule& schedule, epix::ecs::World&) {
            schedule.default_schedule_config().executor_config.on_error = [](const epix::ecs::RunSystemError&) {};
        });

    // The plugin only loads the embedded shader handle; the asset itself is
    // loaded by the main app's asset systems.
    const auto shader = render_app->get().world().get_resource<DownsampleDepthShader>();
    ASSERT_TRUE(shader.has_value());
    for (int attempt = 0;
         attempt < 1000 &&
         !app.resource<epix::assets::AssetServer>().is_loaded_with_dependencies(shader->get().handle.id());
         ++attempt) {
        app.run_schedule(epix::app::PreUpdate);
        app.run_schedule(epix::app::PostUpdate);
        app.run_schedule(epix::app::Last);
        std::this_thread::yield();
    }

    // Force culling support so the creation system always takes the pipeline
    // path, whatever this machine's adapter reports.
    render_app->get().world_mut().insert_resource(epix::render::batching::GpuPreprocessingSupport{
        epix::render::batching::GpuPreprocessingMode::Culling});

    render_app->get().run_schedule(epix::render::RenderStartup);
    // `create_downsample_depth_pipelines` runs in `RenderSystems::Prepare`, as in
    // Bevy, so the render schedule has to run for it. A headless render app logs
    // validation errors for the systems that expect a live frame (frame count,
    // clear color, view attachments); that is a harness limitation and unrelated
    // to this plugin, so the run only needs to reach the prepare set.
    for (int attempt = 0; attempt < 5; ++attempt) {
        render_app->get().run_schedule(epix::render::Render);
        std::this_thread::yield();
    }

    const auto pipelines = render_app->get().world().get_resource<DownsampleDepthPipelines>();
    ASSERT_TRUE(pipelines.has_value());
    EXPECT_FALSE(!pipelines->get().sampler);
    // The plugin specializes all four keys (Bevy `create_downsample_depth_pipelines`).
    EXPECT_TRUE(pipelines->get().first.pipeline_id.has_value());
    EXPECT_TRUE(pipelines->get().second.pipeline_id.has_value());
    EXPECT_TRUE(pipelines->get().first_multisample.pipeline_id.has_value());
    EXPECT_TRUE(pipelines->get().second_multisample.pipeline_id.has_value());

    // The real `downsample_depth` shader must compile, not just load: queue the
    // four descriptors Bevy's plugin specializes and wait for the cache. This
    // runs through the main world's pipeline server because a headless test does
    // not reach the render app's per-frame cache processing.
    auto& pipeline_server = app.resource<epix::render::PipelineServer>();
    const auto compiled = [&](const DownsampleDepthPipeline& pipeline, DownsampleDepthPipelineKey key) {
        const auto id = pipeline_server.queue_compute_pipeline(pipeline.specialize(key));
        for (int attempt = 0; attempt < 200; ++attempt) {
            app.run_schedule(epix::app::PreUpdate);
            app.run_schedule(epix::app::PostUpdate);
            app.run_schedule(epix::app::Last);
            if (pipeline_server.get_compute_pipeline(id).has_value()) return true;
            std::this_thread::yield();
        }
        return false;
    };
    EXPECT_TRUE(compiled(pipelines->get().first, DownsampleDepthPipelineKey{}));
    EXPECT_TRUE(compiled(pipelines->get().second,
                         DownsampleDepthPipelineKey{DownsampleDepthPipelineKey::SECOND_PHASE}));
    EXPECT_TRUE(compiled(pipelines->get().first_multisample,
                         DownsampleDepthPipelineKey{DownsampleDepthPipelineKey::MULTISAMPLE}));
    EXPECT_TRUE(compiled(pipelines->get().second_multisample,
                         DownsampleDepthPipelineKey{DownsampleDepthPipelineKey::MULTISAMPLE |
                                                    DownsampleDepthPipelineKey::SECOND_PHASE}));
}

/** @brief The shader actually reduces the depth buffer: clear a depth texture to
 *  a known value, run both downsample passes over it, read the first pyramid mip
 *  back and check every texel holds that value.
 *
 *  A uniform input cannot distinguish the min-reduction from a passthrough, but
 *  it does prove the whole path end to end: the depth-typed binding, the twelve
 *  storage bindings, the mip-count push constant, both phases (this texture has
 *  seven mips, so the second phase runs), and the storage writes landing in the
 *  pyramid. */
TEST(MipGeneration, DownsampleDepthWritesTheReducedDepthIntoThePyramid) {
    constexpr std::uint32_t kDepthSize   = 128;
    constexpr std::uint32_t kPyramidSize = kDepthSize / 2;
    constexpr float kClearDepth          = 0.25f;

    auto app = epix::app::App::create();
    add_core_graph_test_prerequisites(app);
    try {
        app.add_plugins(epix::render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }
    app.add_plugins(epix::core_graph::mip_generation::MipGenerationPlugin{});

    const auto render_app = app.get_sub_app_mut(epix::render::Render);
    ASSERT_TRUE(render_app.has_value());
    const auto shader = render_app->get().world().get_resource<DownsampleDepthShader>();
    ASSERT_TRUE(shader.has_value());
    for (int attempt = 0;
         attempt < 1000 &&
         !app.resource<epix::assets::AssetServer>().is_loaded_with_dependencies(shader->get().handle.id());
         ++attempt) {
        app.run_schedule(epix::app::PreUpdate);
        app.run_schedule(epix::app::PostUpdate);
        app.run_schedule(epix::app::Last);
        std::this_thread::yield();
    }
    // Compile the two phases through the pipeline server, exactly as the plugin
    // does at runtime.
    auto& pipeline_server = app.resource<epix::render::PipelineServer>();
    const auto layout_desc = create_downsample_depth_bind_group_layout(false);
    const DownsampleDepthPipeline pipeline_desc{
        .bind_group_layout = layout_desc,
        .pipeline_id       = std::nullopt,
        .shader            = shader->get().handle,
    };
    const auto first_id  = pipeline_server.queue_compute_pipeline(
        pipeline_desc.specialize(DownsampleDepthPipelineKey{}));
    const auto second_id = pipeline_server.queue_compute_pipeline(
        pipeline_desc.specialize(DownsampleDepthPipelineKey{DownsampleDepthPipelineKey::SECOND_PHASE}));
    for (int attempt = 0; attempt < 200; ++attempt) {
        app.run_schedule(epix::app::PreUpdate);
        app.run_schedule(epix::app::PostUpdate);
        app.run_schedule(epix::app::Last);
        if (pipeline_server.get_compute_pipeline(first_id).has_value() &&
            pipeline_server.get_compute_pipeline(second_id).has_value()) {
            break;
        }
        std::this_thread::yield();
    }
    const auto first_pipeline  = pipeline_server.get_compute_pipeline(first_id);
    const auto second_pipeline = pipeline_server.get_compute_pipeline(second_id);
    ASSERT_TRUE(first_pipeline.has_value());
    ASSERT_TRUE(second_pipeline.has_value());

    const auto device = render_app->get().world().resource<wgpu::Device>();
    const auto queue  = render_app->get().world().resource<wgpu::Queue>();
    const auto instance = render_app->get().world().resource<wgpu::Instance>();

    const auto depth_texture = device.createTexture(wgpu::TextureDescriptor()
                                                        .setLabel("mip generation test depth")
                                                        .setSize({kDepthSize, kDepthSize, 1})
                                                        .setFormat(wgpu::TextureFormat::eDepth32Float)
                                                        .setUsage(wgpu::TextureUsage::eRenderAttachment |
                                                                  wgpu::TextureUsage::eTextureBinding)
                                                        .setDimension(wgpu::TextureDimension::e2D)
                                                        .setMipLevelCount(1)
                                                        .setSampleCount(1));
    const auto depth_view = depth_texture.createView();

    const std::uint32_t mip_count =
        max_mips_for_size(glm::uvec2(kPyramidSize, kPyramidSize));
    ASSERT_GE(mip_count, 7u) << "the second phase only runs for a seven-mip pyramid";
    const auto pyramid_texture = device.createTexture(wgpu::TextureDescriptor()
                                                          .setLabel("mip generation test pyramid")
                                                          .setSize({kPyramidSize, kPyramidSize, 1})
                                                          .setFormat(wgpu::TextureFormat::eR32Float)
                                                          .setUsage(wgpu::TextureUsage::eStorageBinding |
                                                                    wgpu::TextureUsage::eTextureBinding |
                                                                    wgpu::TextureUsage::eCopySrc)
                                                          .setDimension(wgpu::TextureDimension::e2D)
                                                          .setMipLevelCount(mip_count)
                                                          .setSampleCount(1));

    const auto dummy    = create_depth_pyramid_dummy_texture(device, "test dummy texture",
                                                             "test dummy texture view");
    ViewDepthPyramid pyramid;
    pyramid.all_mips  = pyramid_texture.createView();
    pyramid.mip_count = mip_count;
    for (std::size_t level = 0; level < DEPTH_PYRAMID_MIP_COUNT; ++level) {
        pyramid.mips[level] =
            level < mip_count
                ? pyramid_texture.createView(wgpu::TextureViewDescriptor()
                                                 .setFormat(wgpu::TextureFormat::eR32Float)
                                                 .setDimension(wgpu::TextureViewDimension::e2D)
                                                 .setBaseMipLevel(static_cast<std::uint32_t>(level))
                                                 .setMipLevelCount(1)
                                                 .setBaseArrayLayer(0)
                                                 .setArrayLayerCount(1))
                : dummy;
    }

    const auto sampler = device.createSampler(wgpu::SamplerDescriptor()
                                                  .setLabel("mip generation test sampler")
                                                  .setMagFilter(wgpu::FilterMode::eNearest)
                                                  .setMinFilter(wgpu::FilterMode::eNearest)
                                                  .setMipmapFilter(wgpu::MipmapFilterMode::eNearest));
    const auto bind_group = pyramid.create_bind_group(
        device, "mip generation test bind group", pipeline_server.get_bind_group_layout(layout_desc),
        depth_view, sampler);

    const std::uint32_t row_bytes   = kPyramidSize * sizeof(float);
    const std::uint64_t buffer_size = static_cast<std::uint64_t>(row_bytes) * kPyramidSize;
    const auto readback = device.createBuffer(wgpu::BufferDescriptor()
                                                  .setLabel("mip generation test readback")
                                                  .setSize(buffer_size)
                                                  .setUsage(wgpu::BufferUsage::eCopyDst |
                                                            wgpu::BufferUsage::eMapRead));

    const auto encoder = device.createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("mip test"));
    {
        // Clear the depth buffer to the value every pyramid texel must become.
        wgpu::RenderPassDepthStencilAttachment depth_attachment;
        depth_attachment.setView(depth_view)
            .setDepthClearValue(kClearDepth)
            .setDepthLoadOp(wgpu::LoadOp::eClear)
            .setDepthStoreOp(wgpu::StoreOp::eStore);
        wgpu::RenderPassDescriptor clear_descriptor;
        clear_descriptor.setLabel("mip generation depth clear");
        clear_descriptor.setDepthStencilAttachment(depth_attachment);
        auto pass = encoder.beginRenderPass(clear_descriptor);
        pass.end();
    }
    {
        // Bevy `ViewDepthPyramid::downsample_depth`: the first phase tiles the
        // view in 64x64 workgroups, the second runs one workgroup.
        auto pass = encoder.beginComputePass(wgpu::ComputePassDescriptor().setLabel("downsample depth"));
        pass.setPipeline(first_pipeline->get().pipeline());
        pass.setImmediates(0, &pyramid.mip_count, sizeof(pyramid.mip_count));
        pass.setBindGroup(0, bind_group, std::span<const std::uint32_t>{});
        pass.dispatchWorkgroups((kDepthSize + 63u) / 64u, (kDepthSize + 63u) / 64u, 1);
        if (pyramid.mip_count >= 7u) {
            pass.setPipeline(second_pipeline->get().pipeline());
            pass.dispatchWorkgroups(1, 1, 1);
        }
        pass.end();
    }
    wgpu::TexelCopyTextureInfo source_info;
    source_info.setTexture(pyramid_texture).setMipLevel(0).setOrigin(wgpu::Origin3D{0, 0, 0}).setAspect(
        wgpu::TextureAspect::eAll);
    wgpu::TexelCopyBufferInfo destination_info;
    destination_info.setBuffer(readback).setLayout(wgpu::TexelCopyBufferLayout()
                                                      .setOffset(0)
                                                      .setBytesPerRow(row_bytes)
                                                      .setRowsPerImage(kPyramidSize));
    encoder.copyTextureToBuffer(source_info, destination_info, wgpu::Extent3D{kPyramidSize, kPyramidSize, 1});
    queue.submit(encoder.finish());

    bool mapped = false, succeeded = false;
    (void)readback.mapAsync(wgpu::MapMode::eRead, 0, buffer_size,
                            wgpu::BufferMapCallbackInfo()
                                .setMode(wgpu::CallbackMode::eAllowProcessEvents)
                                .setCallback(wgpu::BufferMapCallback(
                                    [&](wgpu::MapAsyncStatus status, wgpu::StringView) {
                                        succeeded = status == wgpu::MapAsyncStatus::eSuccess;
                                        mapped    = true;
                                    })));
    for (int attempt = 0; attempt < 1000 && !mapped; ++attempt) {
        // Wait for the submission rather than merely polling once: the map
        // callback is delivered through the instance's event loop, which the
        // blocking poll also drives.
        device.poll(true);
        instance.processEvents();
        std::this_thread::yield();
    }
    ASSERT_TRUE(mapped) << "the pyramid readback never completed";
    ASSERT_TRUE(succeeded) << "the pyramid readback failed";

    const auto* values = static_cast<const float*>(readback.getConstMappedRange(0, buffer_size));
    ASSERT_NE(values, nullptr);
    for (std::uint32_t index = 0; index < kPyramidSize * kPyramidSize; ++index) {
        ASSERT_FLOAT_EQ(values[index], kClearDepth) << "pyramid texel " << index;
    }
    readback.unmap();
}

}  // namespace
