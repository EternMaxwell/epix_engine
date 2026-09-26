#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph/skybox.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/window.hpp>
#include <epix/core_graph/core3d.hpp>
#include <gtest/gtest.h>

using namespace epix::core_graph;

namespace {

/** @brief Asset loading spawns tasks on the IO pool, so a test that loads the
 *  embedded shader needs it initialized. */
struct SkyboxTaskPoolInit {
    SkyboxTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPool{epix::task::TaskPoolBuilder{}.num_threads(4).build()});
    }
} g_skybox_task_pool_init;

void add_skybox_test_prerequisites(epix::app::App& app) {
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(epix::camera::CameraPlugin{})
        .add_plugins(epix::assets::AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::render::FrameCountPlugin{});
}

/** @brief The component's defaults are Bevy's: the default image, no brightness
 *  scale and no rotation (`skybox/mod.rs:99-107`). */
TEST(Skybox, DefaultsMatchBevy) {
    const Skybox skybox;
    EXPECT_EQ(skybox.image.id(), epix::image::DEFAULT_IMAGE_HANDLE.id());
    EXPECT_FLOAT_EQ(skybox.brightness, 0.0f);
    EXPECT_FLOAT_EQ(skybox.rotation.x, 0.0f);
    EXPECT_FLOAT_EQ(skybox.rotation.y, 0.0f);
    EXPECT_FLOAT_EQ(skybox.rotation.z, 0.0f);
    EXPECT_FLOAT_EQ(skybox.rotation.w, 1.0f);
}

/** @brief The uniforms hold the exposure-scaled brightness and the inverse
 *  rotation as a matrix, and their layout matches Bevy's `ShaderType` for
 *  `{ f32, Mat4 }` (`skybox/mod.rs:114-148`). */
TEST(Skybox, UniformsLayoutMatchesBevyShaderType) {
    EXPECT_EQ(sizeof(SkyboxUniforms), 80u) << "4 bytes of brightness and padding plus a 64-byte Mat4";
    EXPECT_EQ(offsetof(SkyboxUniforms, brightness), 0u);
    EXPECT_EQ(offsetof(SkyboxUniforms, transform), 16u);

    const SkyboxUniforms uniforms{};
    EXPECT_FLOAT_EQ(uniforms.brightness, 1.0f);
    EXPECT_FLOAT_EQ(uniforms.transform[0][0], 1.0f);
}

/** @brief The specialization key is Bevy's `SkyboxPipelineKey`: the HDR flag,
 *  the sample count and the depth format (`skybox/mod.rs:182-187`). */
TEST(Skybox, PipelineKeyComparesAllThreeFields) {
    const SkyboxPipelineKey base{};
    EXPECT_EQ(base, (SkyboxPipelineKey{}));
    EXPECT_NE(base, (SkyboxPipelineKey{.hdr = true}));
    EXPECT_NE(base, (SkyboxPipelineKey{.samples = 4}));
    EXPECT_NE(base, (SkyboxPipelineKey{.depth_format = wgpu::TextureFormat::eDepth32Float}));

    // The key is a hash-map key for `SpecializedRenderPipelines`.
    const std::hash<SkyboxPipelineKey> hash{};
    EXPECT_EQ(hash(base), hash(SkyboxPipelineKey{}));
    EXPECT_NE(hash(base), hash(SkyboxPipelineKey{.hdr = true}));
}

/** @brief `specialize` writes Bevy's descriptor: one bind group, no depth
 *  writes, a `GreaterEqual` depth compare, the key's sample count and the HDR
 *  or default colour format with no blend (`skybox/mod.rs:189-238`). */
TEST(Skybox, SpecializeBuildsBevyDescriptor) {
    const epix::assets::Handle<epix::shader::Shader> shader{
        epix::assets::AssetId<epix::shader::Shader>::invalid()};
    const auto pipeline = SkyboxPipeline::create(shader);

    // `SkyboxPipeline::new`'s layout: the cube texture and sampler, the dynamic
    // view uniform on both stages, and the skybox uniforms (`:156-175`).
    EXPECT_EQ(pipeline.bind_group_layout.label, "skybox_bind_group_layout");
    ASSERT_EQ(pipeline.bind_group_layout.entries.size(), 4u);
    const auto& entries = pipeline.bind_group_layout.entries;
    EXPECT_EQ(entries[0].binding, 0u);
    EXPECT_EQ(entries[0].texture.viewDimension, wgpu::TextureViewDimension::eCube);
    EXPECT_EQ(entries[0].texture.sampleType, wgpu::TextureSampleType::eFloat);
    EXPECT_EQ(entries[1].sampler.type, wgpu::SamplerBindingType::eFiltering);
    EXPECT_EQ(entries[2].buffer.type, wgpu::BufferBindingType::eUniform);
    EXPECT_EQ(entries[2].buffer.hasDynamicOffset, true);
    EXPECT_EQ(entries[2].visibility, wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment);
    EXPECT_EQ(entries[3].buffer.minBindingSize, sizeof(SkyboxUniforms));

    const auto descriptor = pipeline.specialize(SkyboxPipelineKey{
        .hdr = false, .samples = 4, .depth_format = wgpu::TextureFormat::eDepth32Float});
    EXPECT_EQ(descriptor.label, "skybox_pipeline");
    ASSERT_EQ(descriptor.layouts.size(), 1u);
    ASSERT_TRUE(descriptor.depth_stencil.has_value());
    EXPECT_EQ(descriptor.depth_stencil->format, wgpu::TextureFormat::eDepth32Float);
    EXPECT_EQ(descriptor.depth_stencil->depthWriteEnabled, wgpu::OptionalBool::eFalse);
    EXPECT_EQ(descriptor.depth_stencil->depthCompare, wgpu::CompareFunction::eGreaterEqual);
    EXPECT_EQ(descriptor.multisample.count, 4u);
    ASSERT_TRUE(descriptor.fragment.has_value());
    ASSERT_EQ(descriptor.fragment->targets.size(), 1u);
    EXPECT_EQ(descriptor.fragment->targets[0].format, epix::image::bevy_default<wgpu::TextureFormat>());
    EXPECT_EQ(descriptor.vertex.entry_point, std::optional<std::string>{"skybox_vertex"});
    EXPECT_EQ(descriptor.fragment->entry_point, std::optional<std::string>{"skybox_fragment"});

    const auto hdr = pipeline.specialize(SkyboxPipelineKey{
        .hdr = true, .samples = 1, .depth_format = wgpu::TextureFormat::eDepth32Float});
    ASSERT_TRUE(hdr.fragment.has_value());
    ASSERT_EQ(hdr.fragment->targets.size(), 1u);
    EXPECT_EQ(hdr.fragment->targets[0].format, epix::render::view::ViewTarget::TEXTURE_FORMAT_HDR);
    EXPECT_EQ(hdr.multisample.count, 1u);
}

/** @brief The ported Slang shader compiles into the specialized skybox
 *  pipeline: the cube texture and sampler bindings, the view uniform and the
 *  skybox uniforms all match the layout, and both entry points exist. */
TEST(Skybox, PipelineCompilesWithThePortedShader) {
    auto app = epix::app::App::create();
    add_skybox_test_prerequisites(app);
    try {
        app.add_plugins(epix::render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    app.add_plugins(SkyboxPlugin{});

    const auto render_app = app.get_sub_app_mut(epix::render::Render);
    ASSERT_TRUE(render_app.has_value());
    render_app->get().run_schedule(epix::render::RenderStartup);

    const auto pipeline = render_app->get().world().get_resource<SkyboxPipeline>();
    ASSERT_TRUE(pipeline.has_value()) << "init_skybox_pipeline did not insert SkyboxPipeline";

    auto& asset_server = app.resource<epix::assets::AssetServer>();
    for (int attempt = 0; attempt < 1000 &&
                          !asset_server.is_loaded_with_dependencies(pipeline->get().shader.id());
         ++attempt) {
        app.run_schedule(epix::app::Last);
        std::this_thread::yield();
    }
    ASSERT_TRUE(asset_server.is_loaded_with_dependencies(pipeline->get().shader.id()))
        << "embedded://core_pipeline/skybox.slang failed to load";

    auto& pipeline_server = app.resource<epix::render::PipelineServer>();
    const auto id         = pipeline_server.queue_render_pipeline(pipeline->get().specialize(SkyboxPipelineKey{
        .hdr = false, .samples = 1, .depth_format = epix::core_graph::core_3d::CORE_3D_DEPTH_FORMAT}));
    for (int attempt = 0; attempt < 200; ++attempt) {
        app.run_schedule(epix::app::Last);
        if (pipeline_server.get_render_pipeline(id).has_value()) break;
        std::this_thread::yield();
    }
    EXPECT_TRUE(pipeline_server.get_render_pipeline(id).has_value())
        << "the ported skybox shader failed to compile";
}

/** @brief The C20a probe: an unwritten output location has to be spelled as a
 *  sentinel, because neither the C++ wrapper (`FragmentState::targets` is a
 *  vector of values, `webgpu.hpp:5882-5890`) nor the C ABI
 *  (`WGPUFragmentState::targets` is `WGPUColorTargetState const *` with a count,
 *  `webgpu.h:4794-4801`) can hold a hole. Bevy's prepass skybox writes its
 *  velocity to location 1, so the question is whether wgpu accepts an
 *  `Undefined`-format target in slot 0 while the shader writes `SV_Target1`.
 *  A compiled pipeline here means C20a collapses into a convention rather than a
 *  render-module change; a rejected one means the wrapper has to be bypassed. */
TEST(SkyboxPrepass, UndefinedFormatTargetIsAcceptedAsAnUnwrittenSlot) {
    auto app = epix::app::App::create();
    add_skybox_test_prerequisites(app);
    try {
        app.add_plugins(epix::render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    // A fullscreen triangle and a fragment shader that writes only location 1,
    // exactly the shape of the prepass skybox.
    constexpr std::string_view kProbeSlang = R"slang(
struct VOut {
    float4 pos : SV_Position;
    [[vk::location(0)]] float2 uv;
};
[shader("vertex")]
VOut probe_vertex(uint vertex_index : SV_VulkanVertexID) {
    const float2 uv = float2(float(vertex_index >> 1), float(vertex_index & 1)) * 2.0f;
    VOut out;
    out.uv  = uv;
    out.pos = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return out;
}
[shader("fragment")]
float4 probe_fragment([[vk::location(0)]] float2 uv) : SV_Target1 {
    return float4(uv, 0.0f, 1.0f);
}
)slang";
    app.world_mut()
        .resource_mut<epix::assets::EmbeddedAssetRegistry>()
        .insert_asset_static("core_pipeline/unwritten_target_probe.slang",
                            std::as_bytes(std::span(kProbeSlang)));

    auto& asset_server = app.resource<epix::assets::AssetServer>();
    const auto shader  = asset_server.load<epix::shader::Shader>("embedded://core_pipeline/unwritten_target_probe.slang");
    for (int attempt = 0; attempt < 1000 && !asset_server.is_loaded_with_dependencies(shader.id()); ++attempt) {
        app.run_schedule(epix::app::Last);
        std::this_thread::yield();
    }
    ASSERT_TRUE(asset_server.is_loaded_with_dependencies(shader.id())) << "the probe shader failed to load";

    const auto unwritten =
        wgpu::ColorTargetState().setFormat(wgpu::TextureFormat::eUndefined).setWriteMask(wgpu::ColorWriteMask::eAll);
    const auto written =
        wgpu::ColorTargetState()
            .setFormat(epix::core_graph::core_3d::MOTION_VECTOR_PREPASS_FORMAT)
            .setWriteMask(wgpu::ColorWriteMask::eAll);

    auto& pipeline_server = app.resource<epix::render::PipelineServer>();
    const auto id         = pipeline_server.queue_render_pipeline(epix::render::RenderPipelineDescriptor{
        .label     = "unwritten_target_probe",
        .layouts   = {},
        .vertex    = epix::render::VertexState{.shader = shader, .entry_point = std::string("probe_vertex")},
        .primitive = wgpu::PrimitiveState()
                         .setTopology(wgpu::PrimitiveTopology::eTriangleList)
                         .setFrontFace(wgpu::FrontFace::eCCW)
                         .setCullMode(wgpu::CullMode::eNone)
                         .setUnclippedDepth(false),
        .depth_stencil = std::nullopt,
        .multisample =
            wgpu::MultisampleState().setCount(1).setMask(~0u).setAlphaToCoverageEnabled(false),
        .fragment = epix::render::FragmentState{.shader      = shader,
                                                .entry_point = std::string("probe_fragment"),
                                                .targets     = {unwritten, written}},
    });
    for (int attempt = 0; attempt < 200; ++attempt) {
        app.run_schedule(epix::app::Last);
        if (pipeline_server.get_render_pipeline(id).has_value()) break;
        std::this_thread::yield();
    }
    EXPECT_TRUE(pipeline_server.get_render_pipeline(id).has_value())
        << "wgpu rejected an Undefined-format target as an unwritten output location, so the wrapper has to be "
           "bypassed for Bevy's prepass target sets";
}

}  // namespace
