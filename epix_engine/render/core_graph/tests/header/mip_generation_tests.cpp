#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph/mip_generation.hpp>
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
        epix::render::RenderPlugin{.synchronous_pipeline_compilation = true}.attach(app);
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

}  // namespace
