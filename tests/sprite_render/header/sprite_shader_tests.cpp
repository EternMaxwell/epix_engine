// The sprite pipeline's shader-def variants have to *compile*, not just
// specialize.
//
// `ShaderCache` compiles an entry point per definition set, so a branch such as
// `TONEMAP_IN_SHADER` or `DEBAND_DITHER` is only ever compiled when a pipeline
// asks for it. The pbr mesh shaders proved what that hides: an HLSL
// `SV_VertexID` (Slang then declares `OpCapability DrawParameters`, rejected by
// naga) and column-major packed matrices ("unsupported matrix stride 8") both
// survived every asset-level test because nothing compiled them. This module's
// CPU-side parity test checks the descriptor the key produces, which cannot see
// a rejected module; this test drives `SpritePipeline::specialize` into the real
// `PipelineServer` and waits for the pipelines to become ready.

#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/sprite_render.hpp>
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

using epix::core_graph::DebandDither;
using epix::core_graph::Tonemapping;
using epix::sprite_render::SpritePipeline;
using epix::sprite_render::SpritePipelineKey;

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
        .add_plugins(render::FrameCountPlugin{})
        .add_plugins(render::view::ViewPlugin{})
        .add_plugins(render::GlobalsPlugin{});
}

}  // namespace

TEST(SpritePipeline, SpecializedDescriptorsCompileOnADevice) {
    App app = App::create();
    add_render_prerequisites(app);
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }
    // The sprite pipeline resource (with its shader handle) is created in the
    // render app's `RenderStartup` (`sprite_render/src/render.cpp:714-740`), so
    // that schedule has to run before the pipeline can be specialized.
    // The 2D core graph comes after `RenderPlugin` because it configures the
    // render sub-app, and the sprite plugin follows it: `SpriteRenderPlugin::ready`
    // adds `app_add_render_command<DrawSprite>` (`sprite_render/src/render.cpp:709-712`),
    // which needs the `Transparent2D` phase's `DrawFunctions` to exist first.
    app.add_plugins(epix::core_graph::CoreGraphPlugin{});
    app.add_plugins(epix::sprite_render::SpriteRenderPlugin{});
    // What `App::run` does before any schedule runs: the sprite plugin adds its
    // `RenderStartup` system in `ready` (`sprite_render/src/render.cpp:693-740`),
    // which a headless test never reaches otherwise.
    app.resource_scope([&app](epix::app::internal::Plugins& plugins) { plugins.ready_all(app); });
    app.sub_app_mut(render::Render).then([](App& render_app) { render_app.run_schedule(render::RenderStartup); });
    for (int attempt = 0; attempt < 10000; ++attempt) {
        app.run_schedule(PreUpdate);
        app.run_schedule(PostUpdate);
        app.run_schedule(Last);
        std::this_thread::yield();
    }

    std::optional<SpritePipeline> pipeline;
    app.sub_app_mut(render::Render).then([&pipeline](App& render_app) {
        pipeline = render_app.resource<SpritePipeline>();
    });
    ASSERT_TRUE(pipeline.has_value()) << "the sprite pipeline resource should exist after RenderStartup";

    auto& server   = app.resource<render::PipelineServer>();
    const auto compile = [&app, &server, &pipeline](SpritePipelineKey key, std::string_view name) {
        auto descriptor = pipeline->specialize(key);
        const auto id   = server.queue_render_pipeline(std::move(descriptor));
        for (int attempt = 0; attempt < 400 && !server.get_render_pipeline(id).has_value(); ++attempt) {
            app.run_schedule(PreUpdate);
            app.run_schedule(PostUpdate);
            app.run_schedule(Last);
            std::this_thread::yield();
        }
        EXPECT_TRUE(server.get_render_pipeline(id).has_value())
            << name
            << ": the sprite pipeline never became ready. A definition-gated branch that compiles to a module the "
               "backend rejects fails here and nowhere else.";
    };

    // The four shape families the key selects between: HDR (which turns the
    // in-shader tonemapping off), plain LDR, tonemapping plus dithering, and a
    // multisampled non-HDR view.
    compile(epix::sprite_render::detail::view_pipeline_key(true, 1, std::nullopt, std::nullopt), "hdr");
    compile(epix::sprite_render::detail::view_pipeline_key(false, 1, std::nullopt, std::nullopt), "ldr");
    compile(epix::sprite_render::detail::view_pipeline_key(false, 1, Tonemapping::TonyMcMapface, DebandDither::Enabled),
            "tonemap+deband");
    compile(epix::sprite_render::detail::view_pipeline_key(false, 4, Tonemapping::None, DebandDither::Disabled),
            "msaa4+tonemap-none");
}
