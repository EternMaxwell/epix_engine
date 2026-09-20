#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/core_graph.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/sprite_render/mesh2d.hpp>
#include <epix/sprite_render/mesh2d_pipeline.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <gtest/gtest.h>
#include <webgpu/webgpu.hpp>

#include <array>
#include <string>
#include <thread>
#include <vector>

using namespace epix::app;
namespace render        = epix::render;
namespace sprite_render = epix::sprite_render;
namespace image         = epix::image;
namespace mesh          = epix::mesh;
namespace shader        = epix::shader;
namespace assets        = epix::assets;
namespace core_graph    = epix::core_graph;

namespace {

// Loading an embedded shader spawns an async load task, which terminates the
// process when the IO task pool has not been initialized; RenderPlugin's
// ScreenshotPlugin loads one during attach.
struct IoTaskPoolInit {
    IoTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPool{epix::task::TaskPoolBuilder{}.num_threads(4)});
    }
} g_io_task_pool_init;

/** @brief The plugin set the render smoke test uses before `RenderPlugin`,
 *  plus the shader plugin and the plugins that register the shader libraries
 *  mesh2d imports. `ShaderPlugin` must come first, otherwise the library
 *  plugins cannot create shader handles. */
void add_render_prerequisites(App& app) {
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(epix::camera::CameraPlugin{})
        .add_plugins(epix::assets::AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::shader::ShaderPlugin{})
        .add_plugins(render::FrameCountPlugin{})
        .add_plugins(render::view::ViewPlugin{})
        .add_plugins(render::GlobalsPlugin{})
        .add_plugins(core_graph::TonemappingPlugin{});
}

/** @brief The uniform-array fallback binds one uniform buffer read through a
 *  dynamic offset (Bevy's `PER_OBJECT_BUFFER_BATCH_SIZE` path). */
render::BindGroupLayoutDescriptor create_mesh2d_uniform_mesh_layout() {
    const std::array<wgpu::BindGroupLayoutEntry, 1> entries{
        wgpu::BindGroupLayoutEntry()
            .setBinding(0)
            .setVisibility(wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment)
            .setBuffer(wgpu::BufferBindingLayout()
                           .setType(wgpu::BufferBindingType::eUniform)
                           .setHasDynamicOffset(wgpu::Bool(true))
                           .setMinBindingSize(0)),
    };
    return render::BindGroupLayoutDescriptor().set_label("mesh2d_uniform_layout").set_entries(entries);
}

/** @brief Queue the mesh2d pipeline for one shader-def variant, using the
 *  layouts `init_mesh_2d_pipeline` created (or the uniform fallback's). */
render::CachedPipelineId queue_mesh2d_pipeline(App& app,
                                               const render::BindGroupLayoutDescriptor& view_layout,
                                               const render::BindGroupLayoutDescriptor& mesh_layout,
                                               const assets::Handle<shader::Shader>& shader,
                                               std::vector<shader::ShaderDefVal> shader_defs) {

    // Position (location 0), uv (location 2) and colour (location 4): the
    // attributes the enabled shader defs declare.
    const std::array attributes{
        mesh::VertexAttribute().set_format(wgpu::VertexFormat::eFloat32x3).set_offset(0).set_shader_location(0),
        mesh::VertexAttribute().set_format(wgpu::VertexFormat::eFloat32x2).set_offset(12).set_shader_location(2),
        mesh::VertexAttribute().set_format(wgpu::VertexFormat::eFloat32x4).set_offset(20).set_shader_location(4),
    };
    render::VertexState vertex{
        .shader      = shader,
        .shader_defs = shader_defs,
        .entry_point = std::string("vertex"),
    };
    vertex.set_buffer(0, mesh::VertexBufferLayout()
                             .set_array_stride(36)
                             .set_step_mode(wgpu::VertexStepMode::eVertex)
                             .set_attributes(attributes));

    const auto color_blend = wgpu::BlendComponent()
                                 .setOperation(wgpu::BlendOperation::eAdd)
                                 .setSrcFactor(wgpu::BlendFactor::eSrcAlpha)
                                 .setDstFactor(wgpu::BlendFactor::eOneMinusSrcAlpha);
    const auto alpha_blend = wgpu::BlendComponent()
                                 .setOperation(wgpu::BlendOperation::eAdd)
                                 .setSrcFactor(wgpu::BlendFactor::eOne)
                                 .setDstFactor(wgpu::BlendFactor::eOneMinusSrcAlpha);
    render::FragmentState fragment{
        .shader      = shader,
        .shader_defs = std::move(shader_defs),
        .entry_point = std::string("fragment"),
    };
    fragment.add_target(wgpu::ColorTargetState()
                            .setFormat(image::bevy_default<wgpu::TextureFormat>())
                            .setBlend(wgpu::BlendState().setColor(color_blend).setAlpha(alpha_blend))
                            .setWriteMask(wgpu::ColorWriteMask::eAll));

    render::RenderPipelineDescriptor descriptor{
        .label         = "mesh2d_pipeline_test",
        // TEST PROBE: the example's configuration (3 pipeline layouts, 4x MSAA)
        // instead of the 2-layout/1-sample one this test previously covered.
        .layouts       = {view_layout, mesh_layout, view_layout},
        .vertex        = std::move(vertex),
        .primitive     = wgpu::PrimitiveState()
                             .setTopology(wgpu::PrimitiveTopology::eTriangleList)
                             .setFrontFace(wgpu::FrontFace::eCCW)
                             .setCullMode(wgpu::CullMode::eNone),
        .depth_stencil = wgpu::DepthStencilState()
                             .setFormat(core_graph::core_2d::CORE_2D_DEPTH_FORMAT)
                             .setDepthWriteEnabled(wgpu::OptionalBool::eFalse)
                             .setDepthCompare(wgpu::CompareFunction::eGreaterEqual),
        .multisample   = wgpu::MultisampleState().setCount(4).setMask(~0u).setAlphaToCoverageEnabled(false),
        .fragment      = std::move(fragment),
    };
    return app.resource<render::PipelineServer>().queue_render_pipeline(std::move(descriptor));
}

}  // namespace

// Drives the real path: `register_mesh2d_shader` publishes the embedded bytes and
// `init_mesh_2d_pipeline` runs as a RenderStartup system that loads the shader
// from the render world's asset server (Bevy `mesh.rs:292-327`). The shader is
// only compiled when a pipeline is specialized against it, so the test queues the
// vertex-colour and tonemapped variants: a Slang error, or a failure to resolve
// `import epix.view` / `import epix.globals` / `import epix.tonemapping`, fails
// here rather than at the draw-command step.
TEST(Mesh2dShader, CompilesAfterInitMesh2dPipelineLoadsIt) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    add_render_prerequisites(app);
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    // The public path: Mesh2dRenderPlugin registers the embedded shader and adds
    // init_mesh_2d_pipeline to the render app's RenderStartup, as Bevy's build does.
    app.add_plugins(sprite_render::Mesh2dRenderPlugin{});

    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());

    auto& server = app.resource<assets::AssetServer>();
    // RenderStartup is one-shot and driven by the render app's first frame, which
    // a headless test does not reach, so run the schedule directly.
    render_app->get().run_schedule(render::RenderStartup);
    for (int attempt = 0; attempt < 500; ++attempt) {
        app.run_schedule(Last);
        const auto pipeline = render_app->get().world().get_resource<sprite_render::Mesh2dPipeline>();
        if (pipeline && server.is_loaded_with_dependencies(pipeline->get().shader.id())) break;
        std::this_thread::yield();
    }

    const auto pipeline = render_app->get().world().get_resource<sprite_render::Mesh2dPipeline>();
    ASSERT_TRUE(pipeline.has_value()) << "init_mesh_2d_pipeline did not insert Mesh2dPipeline";
    const auto shader_handle = pipeline->get().shader;
    ASSERT_TRUE(server.is_loaded_with_dependencies(shader_handle.id()))
        << "embedded://sprite/mesh2d.slang failed to load";

    const auto device = render_app->get().world().resource<wgpu::Device>();

    // `Mesh2dPipeline` exposes the layouts and the uniform-fallback batch size
    // that `init_mesh_2d_pipeline` resolved through the interning cache.
    EXPECT_EQ(pipeline->get().view_layout.label, "mesh2d_view_layout");
    EXPECT_EQ(pipeline->get().mesh_layout.label, "mesh2d_layout");
    wgpu::Limits limits{};
    device.getLimits(&limits);
    EXPECT_EQ(pipeline->get().per_object_buffer_batch_size.has_value(),
              limits.maxStorageBuffersPerShaderStage == 0);

    // The imported libraries load asynchronously; until they are available the
    // pipeline sits in the recoverable "import not yet available" state, so wait
    // for them before specializing.
    const auto view_library    = server.load<shader::Shader>("embedded://epix/shaders/view.slang");
    const auto globals_library = server.load<shader::Shader>("embedded://epix/shaders/globals.slang");
    // `epix/tonemapping` is the shared library (`tonemapping_shared.slang`), not
    // the fragment shader of the same directory.
    const auto tonemapping_library =
        server.load<shader::Shader>("embedded://core_pipeline/tonemapping_shared.slang");
    for (int attempt = 0; attempt < 500; ++attempt) {
        app.run_schedule(Last);
        if (server.is_loaded_with_dependencies(view_library.id()) &&
            server.is_loaded_with_dependencies(globals_library.id()) &&
            server.is_loaded_with_dependencies(tonemapping_library.id())) {
            break;
        }
        std::this_thread::yield();
    }

    const std::vector<shader::ShaderDefVal> vertex_color_defs{
        shader::ShaderDefVal::from_bool("VERTEX_POSITIONS"),
        shader::ShaderDefVal::from_bool("VERTEX_UVS"),
        shader::ShaderDefVal::from_bool("VERTEX_COLORS"),
    };
    const auto& mesh2d_pipeline = pipeline->get();
    const auto& view_layout     = mesh2d_pipeline.view_layout;

    auto plain           = queue_mesh2d_pipeline(app, view_layout, mesh2d_pipeline.mesh_layout, shader_handle,
                                                 vertex_color_defs);
    auto tonemapped_defs = vertex_color_defs;
    tonemapped_defs.push_back(shader::ShaderDefVal::from_bool("TONEMAP_IN_SHADER"));
    tonemapped_defs.push_back(shader::ShaderDefVal::from_bool("TONEMAP_METHOD_TONY_MC_MAPFACE"));
    auto tonemapped = queue_mesh2d_pipeline(app, view_layout, mesh2d_pipeline.mesh_layout, shader_handle,
                                            std::move(tonemapped_defs));

    // Bevy's no-storage-buffer variant: the same vertex path reading the mesh
    // array out of a uniform buffer, so the layout differs from the resource's.
    const auto uniform_mesh_layout = create_mesh2d_uniform_mesh_layout();
    auto fallback_defs             = vertex_color_defs;
    fallback_defs.push_back(shader::ShaderDefVal::from_uint("PER_OBJECT_BUFFER_BATCH_SIZE", 682));
    auto fallback = queue_mesh2d_pipeline(app, view_layout, uniform_mesh_layout, shader_handle,
                                          std::move(fallback_defs));

    // The example meshes do not all carry the same attributes (`make_box2d` has
    // positions+uvs, `make_gradient_quad`/`make_circle` have positions+colours),
    // so the pipeline is specialized once per attribute subset. Those subsets
    // were previously untested, and they are the variants that a real scene
    // exercises.
    const std::vector<shader::ShaderDefVal> uvs_only_defs{
        shader::ShaderDefVal::from_bool("VERTEX_POSITIONS"),
        shader::ShaderDefVal::from_bool("VERTEX_UVS"),
        shader::ShaderDefVal::from_bool("TONEMAP_IN_SHADER"),
        shader::ShaderDefVal::from_bool("TONEMAP_METHOD_TONY_MC_MAPFACE"),
    };
    auto uvs_only = queue_mesh2d_pipeline(app, view_layout, mesh2d_pipeline.mesh_layout, shader_handle, uvs_only_defs);

    const std::vector<shader::ShaderDefVal> colors_only_defs{
        shader::ShaderDefVal::from_bool("VERTEX_POSITIONS"),
        shader::ShaderDefVal::from_bool("VERTEX_COLORS"),
        shader::ShaderDefVal::from_bool("TONEMAP_IN_SHADER"),
        shader::ShaderDefVal::from_bool("TONEMAP_METHOD_TONY_MC_MAPFACE"),
    };
    auto colors_only =
        queue_mesh2d_pipeline(app, view_layout, mesh2d_pipeline.mesh_layout, shader_handle, colors_only_defs);

    {
        auto& pipelines = app.resource_mut<render::PipelineServer>();
        pipelines.block_on_render_pipeline(plain);
        pipelines.block_on_render_pipeline(tonemapped);
        pipelines.block_on_render_pipeline(fallback);
        pipelines.block_on_render_pipeline(uvs_only);
        pipelines.block_on_render_pipeline(colors_only);
    }
    const auto& pipelines = app.resource<render::PipelineServer>();

    const auto describe = [&pipelines](render::CachedPipelineId id) {
        using Error = shader::ShaderCacheError;
        const auto state = pipelines.get_pipeline_state(id);
        if (!state) return std::string("no cached state");
        if (std::holds_alternative<render::PipelineStateQueued>(state->get())) return std::string("still queued");
        const auto* recoverable = std::get_if<render::PipelineStateRecoverableShaderError>(&state->get());
        if (!recoverable) return std::string("state variant ") + std::to_string(state->get().index());
        const auto& error = recoverable->error.data;
        if (const auto* slang = std::get_if<Error::SlangCompileError>(&error)) {
            return "Slang stage " + std::to_string(static_cast<int>(slang->stage)) + ": " + slang->message;
        }
        if (const auto* imports = std::get_if<Error::ShaderImportNotYetAvailable>(&error)) {
            std::string missing;
            for (const auto& import : imports->missing_imports) {
                missing += " ";
                if (const auto* path = std::get_if<assets::AssetPath>(&import.data)) {
                    missing += path->path.generic_string();
                } else if (const auto* name = std::get_if<std::filesystem::path>(&import.data)) {
                    missing += name->generic_string();
                }
            }
            return "missing imports (" + std::to_string(imports->missing_imports.size()) + "):" + missing;
        }
        if (const auto* module = std::get_if<Error::CreateShaderModule>(&error)) {
            return "create shader module: " + module->wgpu_message;
        }
        return std::string("shader error variant ") + std::to_string(error.index());
    };
    EXPECT_TRUE(pipelines.get_render_pipeline(plain).has_value())
        << "mesh2d shader failed to compile for the vertex-colour variant: " << describe(plain);
    EXPECT_TRUE(pipelines.get_render_pipeline(tonemapped).has_value())
        << "mesh2d shader failed to compile with TONEMAP_IN_SHADER: " << describe(tonemapped);
    EXPECT_TRUE(pipelines.get_render_pipeline(fallback).has_value())
        << "mesh2d shader failed to compile for the uniform-array fallback: " << describe(fallback);
    EXPECT_TRUE(pipelines.get_render_pipeline(uvs_only).has_value())
        << "mesh2d shader failed to compile for the positions+uvs variant: " << describe(uvs_only);
    EXPECT_TRUE(pipelines.get_render_pipeline(colors_only).has_value())
        << "mesh2d shader failed to compile for the positions+colours variant: " << describe(colors_only);
}
