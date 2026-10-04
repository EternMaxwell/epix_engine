// The core pipeline's fullscreen pipelines have to *compile*, not just
// specialize.
//
// `ShaderCache` compiles an entry point per definition set, so a branch such as
// `TONEMAP_METHOD_*`, `DEBAND_DITHER` or `MULTISAMPLED` is only compiled when a
// pipeline asks for it. The pbr mesh shaders proved what that hides: an HLSL
// `SV_VertexID` (Slang then declares `OpCapability DrawParameters`, which naga
// rejects) and column-major packed matrices ("unsupported matrix stride 8")
// both survived every asset-level test because nothing compiled them. The
// skybox test in this directory already compiles its own pipelines; this covers
// the blit and tonemapping pipelines that every camera's frame ends with.

#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/window.hpp>
#include <gtest/gtest.h>

#include <optional>
#include <string_view>
#include <thread>
#include <utility>

using namespace epix::app;
using namespace epix::assets;

namespace render = epix::render;

using epix::core_graph::BlitPipeline;
using epix::core_graph::BlitPipelineKey;
using epix::core_graph::DebandDither;
using epix::core_graph::Tonemapping;
using epix::core_graph::TonemappingPipeline;
using epix::core_graph::TonemappingPipelineKey;

namespace {

// Loading an embedded shader spawns an async load task, which terminates the
// process when the IO task pool has not been initialized.
struct IoTaskPoolInit {
    IoTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPool{epix::task::TaskPoolBuilder{}.num_threads(4)});
    }
} g_io_task_pool_init;

/** @brief The plugin set this module's device tests need before `RenderPlugin`.
 *  `ShaderPlugin` comes first because the library plugins create shader handles
 *  through it. */
void add_render_prerequisites(App& app) {
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(epix::camera::CameraPlugin{})
        .add_plugins(epix::assets::AssetPlugin{})
        .add_plugins(epix::mesh::MeshPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::shader::ShaderPlugin{})
        .add_plugins(render::FrameCountPlugin{});
}

/** @brief Wait until the render world has the resource, running the main
 *  schedule so asynchronous shader loads progress. */
template <typename T>
std::optional<T> wait_for_render_resource(App& app) {
    auto render_app = app.get_sub_app_mut(render::Render);
    if (!render_app.has_value()) return std::nullopt;
    for (int attempt = 0; attempt < 2000; ++attempt) {
        if (const auto resource = render_app->get().world().get_resource<T>()) return *resource;
        app.run_schedule(PreUpdate);
        app.run_schedule(PostUpdate);
        app.run_schedule(Last);
        std::this_thread::yield();
    }
    if (const auto resource = render_app->get().world().get_resource<T>()) return *resource;
    return std::nullopt;
}

}  // namespace

TEST(CoreGraphShaders, BlitAndTonemappingPipelinesCompileOnADevice) {
    App app = App::create();
    add_render_prerequisites(app);
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }
    // The umbrella installs the fullscreen shader, the blit and tonemapping
    // pipeline resources and the LUT, in the render app's `RenderStartup`.
    app.add_plugins(epix::core_graph::CoreGraphPlugin{});
    // What `App::run` does before any schedule runs: the plugin's `ready` adds
    // those `RenderStartup` systems, which a headless test never reaches.
    app.resource_scope([&app](epix::app::internal::Plugins& plugins) { plugins.ready_all(app); });
    app.sub_app_mut(render::Render).then([](App& render_app) { render_app.run_schedule(render::RenderStartup); });

    const auto blit        = wait_for_render_resource<BlitPipeline>(app);
    const auto tonemapping = wait_for_render_resource<TonemappingPipeline>(app);
    ASSERT_TRUE(blit.has_value()) << "RenderStartup should have created the blit pipeline";
    ASSERT_TRUE(tonemapping.has_value()) << "RenderStartup should have created the tonemapping pipeline";

    auto& server = app.resource<render::PipelineServer>();
    const auto compile = [&app, &server](const auto& pipeline, const auto& key, std::string_view name) {
        auto descriptor = pipeline.specialize(key);
        const auto id   = server.queue_render_pipeline(std::move(descriptor));
        for (int attempt = 0; attempt < 400 && !server.get_render_pipeline(id).has_value(); ++attempt) {
            app.run_schedule(PreUpdate);
            app.run_schedule(PostUpdate);
            app.run_schedule(Last);
            std::this_thread::yield();
        }
        EXPECT_TRUE(server.get_render_pipeline(id).has_value())
            << name
            << ": the pipeline never became ready. A definition-gated branch that compiles to a module the backend "
               "rejects fails here and nowhere else.";
    };

    // The blit's own shape family: the target format, whether it blends, and the
    // sample count, which is the `MULTISAMPLED` branch of its shader.
    compile(*blit, BlitPipelineKey{.texture_format = wgpu::TextureFormat::eRGBA8Unorm}, "blit rgba8unorm");
    compile(*blit, BlitPipelineKey{.texture_format = wgpu::TextureFormat::eRGBA8Unorm, .samples = 4},
            "blit rgba8unorm msaa4");
    compile(*blit,
            BlitPipelineKey{.texture_format = wgpu::TextureFormat::eRGBA16Float,
                            .blend_state  = std::nullopt,
                            .samples      = 1},
            "blit rgba16float");

    // Tonemapping: each method is a different shader definition, and deband
    // dithering adds another.
    const auto tonemap_key = [](Tonemapping method, DebandDither dither) {
        return TonemappingPipelineKey{.deband_dither = dither, .tonemapping = method};
    };
    compile(*tonemapping, tonemap_key(Tonemapping::None, DebandDither::Disabled), "tonemapping none");
    compile(*tonemapping, tonemap_key(Tonemapping::Reinhard, DebandDither::Disabled), "tonemapping reinhard");
    compile(*tonemapping, tonemap_key(Tonemapping::TonyMcMapface, DebandDither::Enabled),
            "tonemapping tony-mc-mapface + deband");
}
