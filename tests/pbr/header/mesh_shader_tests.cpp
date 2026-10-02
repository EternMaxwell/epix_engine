#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/pbr/mesh_pipeline_resource.hpp>
#include <epix/pbr/mesh_shaders.hpp>
#include <epix/render.hpp>
#include <epix/render/view.hpp>
#include <epix/shader.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/window.hpp>
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

using namespace epix::app;
using namespace epix::assets;
using namespace epix::shader;

namespace pbr      = epix::pbr;
namespace mesh     = epix::mesh;
namespace render   = epix::render;
namespace core_graph = epix::core_graph;

namespace {

// Loading an embedded shader spawns an async load task, which terminates the
// process when the IO task pool has not been initialized.
struct IoTaskPoolInit {
    IoTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPool{epix::task::TaskPoolBuilder{}.num_threads(4)});
    }
} g_io_task_pool_init;

App make_shader_asset_app() {
    App app = App::create();

    AssetPlugin asset_plugin;
    asset_plugin.mode = AssetServerMode::Unprocessed;
    app.add_plugins(asset_plugin);

    ShaderPlugin shader_plugin;
    app.add_plugins(shader_plugin);

    return app;
}

void flush_load_tasks(App& app) {
    static constexpr int LARGE_ITERATION_COUNT = 10000;
    for (int i = 0; i < LARGE_ITERATION_COUNT; ++i) {
        app.run_schedule(Last);
        std::this_thread::yield();
    }
}

void expect_embedded_shader_loaded(App& app, std::string_view asset_path) {
    auto& server = app.resource<AssetServer>();
    auto handle  = server.get_handle<Shader>(AssetPath(std::string(asset_path)));
    ASSERT_TRUE(handle.has_value()) << asset_path;
    EXPECT_TRUE(server.is_loaded_with_dependencies(handle->id())) << asset_path;

    auto& shaders = app.resource<Assets<Shader>>();
    auto shader   = shaders.get(handle->id());
    ASSERT_TRUE(shader.has_value()) << asset_path;
    EXPECT_EQ(shader->get().path, asset_path);
}

}  // namespace

/** @brief The mesh shader modules register through the embedded registry and
 *  load through the asset server, and the one module that imports another
 *  resolves it (Bevy `render/mesh.rs:131,294-299`, the two
 *  `load_shader_library!` calls plus the `mesh_types.wgsl` module they import).
 *
 *  This is the GPU-free half of the check the 2D shader test performs: a
 *  module whose `import epix.mesh_types;` cannot be resolved leaves
 *  `is_loaded_with_dependencies` false, so the import graph is verified even
 *  though nothing compiles a pipeline against the modules yet. */
TEST(MeshShaders, RegistersAndLoadsEmbeddedModulesThroughAssetServer) {
    auto app = make_shader_asset_app();

    pbr::register_mesh_shaders(app.world_mut());
    flush_load_tasks(app);

    expect_embedded_shader_loaded(app, "embedded://pbr/mesh_types.slang");
    expect_embedded_shader_loaded(app, "embedded://pbr/mesh_bindings.slang");
    expect_embedded_shader_loaded(app, "embedded://pbr/forward_io.slang");
    expect_embedded_shader_loaded(app, "embedded://pbr/mesh_view_types.slang");
    expect_embedded_shader_loaded(app, "embedded://pbr/atmosphere_types.slang");
    expect_embedded_shader_loaded(app, "embedded://pbr/mesh_view_bindings.slang");
    expect_embedded_shader_loaded(app, "embedded://pbr/occlusion_culling.slang");
    expect_embedded_shader_loaded(app, "embedded://pbr/morph.slang");
    expect_embedded_shader_loaded(app, "embedded://pbr/skinning.slang");
    expect_embedded_shader_loaded(app, "embedded://pbr/prepass_bindings.slang");
    expect_embedded_shader_loaded(app, "embedded://pbr/view_transformations.slang");
    expect_embedded_shader_loaded(app, "embedded://pbr/mesh_functions.slang");
    // The lightmap library imports `epix.mesh_bindings`, so its import graph is what
    // this checks as well (Bevy `lightmap.wgsl:1-11`).
    expect_embedded_shader_loaded(app, "embedded://pbr/lightmap.slang");
    // The forward mesh shader is a root: it has entry points rather than a
    // module declaration, and it imports the libraries above.
    expect_embedded_shader_loaded(app, "embedded://pbr/mesh.slang");
    // The handles resource holds every loaded module.
    const auto& handles = app.resource<pbr::MeshShaderHandles>();
    auto& shaders       = app.resource<Assets<Shader>>();
    const auto types    = shaders.get(handles.mesh_types.id());
    const auto bindings = shaders.get(handles.mesh_bindings.id());
    const auto io       = shaders.get(handles.forward_io.id());
    const auto view_types       = shaders.get(handles.mesh_view_types.id());
    const auto atmosphere_types = shaders.get(handles.atmosphere_types.id());
    const auto view_bindings    = shaders.get(handles.mesh_view_bindings.id());
    const auto culling          = shaders.get(handles.occlusion_culling.id());
    const auto morph            = shaders.get(handles.morph.id());
    const auto skinning         = shaders.get(handles.skinning.id());
    const auto lightmap         = shaders.get(handles.lightmap.id());
    ASSERT_TRUE(types.has_value());
    ASSERT_TRUE(bindings.has_value());
    ASSERT_TRUE(io.has_value());
    ASSERT_TRUE(view_types.has_value());
    ASSERT_TRUE(atmosphere_types.has_value());
    ASSERT_TRUE(view_bindings.has_value());
    ASSERT_TRUE(culling.has_value());
    ASSERT_TRUE(morph.has_value());
    ASSERT_TRUE(skinning.has_value());
    ASSERT_TRUE(lightmap.has_value());

    // `mesh_types`, `forward_io`, `mesh_view_types` and `atmosphere_types` are
    // leaves; `mesh_bindings` declares exactly one import, and it is a custom
    // module name rather than a file path, which is what
    // `import epix.mesh_types;` has to resolve to.
    EXPECT_TRUE(types->get().imports.empty());
    EXPECT_TRUE(io->get().imports.empty());
    EXPECT_TRUE(view_types->get().imports.empty());
    EXPECT_TRUE(atmosphere_types->get().imports.empty());
    ASSERT_EQ(bindings->get().imports.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<std::filesystem::path>(bindings->get().imports[0].data));
    // The view bindings are the module that ties the others together: the two
    // pbr type modules and the engine's own view and globals modules, which is
    // what `is_loaded_with_dependencies` above proves resolved.
    ASSERT_EQ(view_bindings->get().imports.size(), 4u);
    for (const auto& import : view_bindings->get().imports) {
        EXPECT_TRUE(std::holds_alternative<std::filesystem::path>(import.data));
    }

    // Every module names itself, so a material shader can import it by name.
    EXPECT_TRUE(std::holds_alternative<std::filesystem::path>(types->get().import_path.data));
    EXPECT_TRUE(std::holds_alternative<std::filesystem::path>(bindings->get().import_path.data));
    EXPECT_TRUE(std::holds_alternative<std::filesystem::path>(io->get().import_path.data));
    EXPECT_TRUE(std::holds_alternative<std::filesystem::path>(view_types->get().import_path.data));
    EXPECT_TRUE(std::holds_alternative<std::filesystem::path>(atmosphere_types->get().import_path.data));
}

/** @brief The mesh pipeline resource holds Bevy's eight fields and derives four
 *  of them from the device (Bevy `MeshPipeline` and its `FromWorld`,
 *  `bevy_pbr/src/render/mesh.rs:1847-1920`).
 *
 *  `make` is the `FromWorld` counterpart: `limits` stands in for
 *  `RenderDevice::limits()`, the two binding facts for `RenderAdapter`'s
 *  capabilities, and the shader handle for the `mesh.wgsl` the plugin loads —
 *  any loaded handle stands in for that until `mesh.wgsl` itself is ported. */
TEST(MeshPipeline, FieldsAndViewLayoutFollowBevy) {
    auto app = make_shader_asset_app();
    pbr::register_mesh_shaders(app.world_mut());
    flush_load_tasks(app);

    const auto& handles = app.resource<pbr::MeshShaderHandles>();
    auto& shaders       = app.resource<Assets<Shader>>();
    ASSERT_TRUE(shaders.get(handles.forward_io.id()).has_value());

    const auto layouts = pbr::generate_view_layouts(wgpu::BufferBindingType::eReadOnlyStorage,
                                                    wgpu::BufferBindingType::eReadOnlyStorage, true, true, true, false);

    // Default limits have no storage buffers, so both derivations take the
    // fallback a WebGL2-class device takes: a batched uniform buffer and
    // uniform-buffer skins.
    wgpu::Limits uniform_limits{};
    uniform_limits.maxStorageBuffersPerShaderStage = 0;
    const auto uniform_pipeline = pbr::MeshPipeline::make(
        uniform_limits, wgpu::BufferBindingType::eReadOnlyStorage, handles.forward_io, pbr::MeshPipelineViewLayouts{layouts}, true, true);
    EXPECT_EQ(uniform_pipeline.clustered_forward_buffer_binding_type, wgpu::BufferBindingType::eReadOnlyStorage);
    EXPECT_TRUE(uniform_pipeline.binding_arrays_are_usable);
    EXPECT_TRUE(uniform_pipeline.clustered_decals_are_usable);
    EXPECT_TRUE(uniform_pipeline.skins_use_uniform_buffers)
        << "no storage buffers means the uniform path; Bevy memoises this in a OnceLock, so this call fixes it for the process";
    EXPECT_TRUE(uniform_pipeline.per_object_buffer_batch_size.has_value());
    EXPECT_EQ(uniform_pipeline.mesh_layouts.model_only.label, "mesh_layout");

    // The view layouts are indexed by the key, exactly as the resource Bevy's
    // `FromWorld` clones.
    const auto bare_key = pbr::MeshPipelineViewLayoutKey::from_bits_retain(0);
    EXPECT_EQ(uniform_pipeline.get_view_layout(bare_key).main_layout.label, bare_key.label());
    EXPECT_EQ(uniform_pipeline.get_view_layout(bare_key).binding_array_layout.label,
              bare_key.label() + "_binding_array");
    EXPECT_EQ(uniform_pipeline.get_view_layout(pbr::MeshPipelineViewLayoutKey::ATMOSPHERE).main_layout.label,
              pbr::MeshPipelineViewLayoutKey::ATMOSPHERE.label());

    // A storage-capable device takes the other branch of both derivations, and
    // the two capability facts are the ones passed in.
    wgpu::Limits storage_limits{};
    storage_limits.maxStorageBuffersPerShaderStage = 8;
    const auto storage_pipeline = pbr::MeshPipeline::make(
        storage_limits, wgpu::BufferBindingType::eStorage, handles.forward_io, pbr::MeshPipelineViewLayouts{layouts}, false, false);
    EXPECT_FALSE(storage_pipeline.per_object_buffer_batch_size.has_value());
    // `skins_use_uniform_buffers` is memoised on its first call, exactly as Bevy
    // memoises it, so it still answers for the uniform-limits device.
    EXPECT_EQ(storage_pipeline.skins_use_uniform_buffers, uniform_pipeline.skins_use_uniform_buffers);
    EXPECT_FALSE(storage_pipeline.binding_arrays_are_usable);
    EXPECT_FALSE(storage_pipeline.clustered_decals_are_usable);
    EXPECT_EQ(storage_pipeline.clustered_forward_buffer_binding_type, wgpu::BufferBindingType::eStorage);
}

TEST(MeshPipeline, SpecializeAssemblesDescriptorFollowBevy) {
    // Bevy's `SpecializedMeshPipeline for MeshPipeline` (`render/mesh.rs:2372-2740`):
    // the descriptor takes the key's view layouts, the mesh layout the morph and
    // skinning step selects, one vertex buffer built from the mesh's attributes,
    // the HDR or default target format, and core3d's reverse-Z depth state.
    auto app = make_shader_asset_app();
    pbr::register_mesh_shaders(app.world_mut());
    flush_load_tasks(app);

    const auto& handles = app.resource<pbr::MeshShaderHandles>();
    const auto layouts  = pbr::generate_view_layouts(wgpu::BufferBindingType::eReadOnlyStorage,
                                                    wgpu::BufferBindingType::eReadOnlyStorage, true, true, true, false);

    wgpu::Limits limits{};
    limits.maxStorageBuffersPerShaderStage = 0;
    const auto pipeline                    = pbr::MeshPipeline::make(
        limits, wgpu::BufferBindingType::eReadOnlyStorage, handles.mesh, pbr::MeshPipelineViewLayouts{layouts}, true, true);

    // A real mesh's layout: the specialization asks the layout which attributes
    // the mesh has, and the vertex buffer layout it returns carries the strides.
    mesh::MeshVertexBufferLayouts mesh_layouts;
    const auto source = mesh::make_box2d(1.0f, 1.0f);
    const auto layout = source.get_mesh_vertex_buffer_layout(mesh_layouts);
    ASSERT_TRUE(layout.value);

    const pbr::MeshPipelineKey opaque = pbr::MeshPipelineKey::NONE;
    auto descriptor                   = pipeline.specialize(opaque, layout);
    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(descriptor->label, "opaque_mesh_pipeline");

    // The view's main layout, its binding-array layout, then the mesh layout the
    // key selects - the model-only one for a mesh that is neither skinned nor
    // morphed nor lightmapped.
    ASSERT_EQ(descriptor->layouts.size(), 3u);
    EXPECT_EQ(descriptor->layouts[0].label,
              pipeline.get_view_layout(pbr::MeshPipelineViewLayoutKey::from_pipeline_key(opaque)).main_layout.label);
    EXPECT_EQ(descriptor->layouts[2].label, pipeline.mesh_layouts.model_only.label);

    EXPECT_EQ(descriptor->vertex.shader.id(), handles.mesh.id());
    ASSERT_EQ(descriptor->vertex.buffers.size(), 1u);
    EXPECT_EQ(descriptor->vertex.buffers[0].attributes.size(), layout.value->attribute_ids().size());
    ASSERT_TRUE(descriptor->vertex.entry_point.has_value());
    EXPECT_EQ(*descriptor->vertex.entry_point, "vertex");

    ASSERT_TRUE(descriptor->depth_stencil.has_value());
    EXPECT_EQ(descriptor->depth_stencil->format, core_graph::core_3d::CORE_3D_DEPTH_FORMAT);
    EXPECT_EQ(descriptor->depth_stencil->depthCompare, wgpu::CompareFunction::eGreaterEqual);
    EXPECT_EQ(descriptor->depth_stencil->depthWriteEnabled, wgpu::OptionalBool::eTrue);
    EXPECT_EQ(descriptor->multisample.count, 1u);

    // The four texture-feature definitions Bevy gates on cargo features are not
    // in its default feature set, so a default build emits none of them
    // (`PBR-244`); pushing one would send the shader down a path Bevy's default
    // build never compiles.
    const auto has_def = [&descriptor](std::string_view name) {
        return std::ranges::any_of(descriptor->vertex.shader_defs, [name](const ShaderDefVal& def) {
            return def.name == name;
        });
    };
    for (const auto* name : {"PBR_TRANSMISSION_TEXTURES_SUPPORTED", "PBR_MULTI_LAYER_MATERIAL_TEXTURES_SUPPORTED",
                             "PBR_ANISOTROPY_TEXTURE_SUPPORTED", "PBR_SPECULAR_TEXTURES_SUPPORTED"}) {
        EXPECT_FALSE(has_def(name)) << name;
    }
    EXPECT_TRUE(has_def("MESH_PIPELINE"));
    EXPECT_TRUE(has_def("VERTEX_OUTPUT_INSTANCE_INDEX"));
    // The per-object buffer batch size is a valued definition the shader needs.
    EXPECT_TRUE(has_def("PER_OBJECT_BUFFER_BATCH_SIZE"));

    // HDR views render to the HDR target format.
    auto hdr = pipeline.specialize(opaque | pbr::MeshPipelineKey::HDR, layout);
    ASSERT_TRUE(hdr.has_value());
    ASSERT_TRUE(hdr->fragment.has_value());
    ASSERT_EQ(hdr->fragment->targets.size(), 1u);
    EXPECT_EQ(hdr->fragment->targets[0].format, render::view::ViewTarget::TEXTURE_FORMAT_HDR);

    // A transparent key blends and must not write depth.
    auto alpha = pipeline.specialize(pbr::MeshPipelineKey::BLEND_ALPHA, layout);
    ASSERT_TRUE(alpha.has_value());
    EXPECT_EQ(alpha->label, "alpha_blend_mesh_pipeline");
    ASSERT_TRUE(alpha->depth_stencil.has_value());
    EXPECT_EQ(alpha->depth_stencil->depthWriteEnabled, wgpu::OptionalBool::eFalse);
}

/** @brief The plugin registers the same shader set the bare call does (Bevy
 *  `MeshRenderPlugin::build`'s `load_shader_library!` calls and its
 *  `embedded_asset!(app, "mesh.wgsl")`, `render/mesh.rs:129-150`).
 *
 *  Bevy publishes those libraries from the plugin, so a port that registered them
 *  only in a helper would leave the plugin incomplete in a way no pipeline would
 *  report until something compiled it. This drives the plugin itself, device-free,
 *  since none of this half needs a device. */
TEST(MeshShaders, TheMeshRenderPluginRegistersTheSameShaderSet) {
    auto app = make_shader_asset_app();

    // Bevy's `MeshRenderPlugin::new` starts GPU building off and takes only the
    // flags, so `make` must not be the way to request GPU building.
    const auto from_new = pbr::MeshRenderPlugin::make(true);
    EXPECT_TRUE(from_new.debug_flags);
    EXPECT_FALSE(from_new.use_gpu_instance_buffer_builder)
        << "Bevy's constructor leaves GPU building off; the plugin group sets it";

    app.add_plugins(pbr::MeshRenderPlugin{});
    flush_load_tasks(app);

    expect_embedded_shader_loaded(app, "embedded://pbr/mesh_types.slang");
    expect_embedded_shader_loaded(app, "embedded://pbr/mesh_bindings.slang");
    expect_embedded_shader_loaded(app, "embedded://pbr/mesh.slang");
    expect_embedded_shader_loaded(app, "embedded://pbr/lightmap.slang");
    // The handles resource the rest of the port reads from is populated by the same
    // call, which the assertions above already imply.
}

/** @brief The specialized mesh pipelines have to *compile* on a device, not just
 *  assemble a descriptor.
 *
 *  `ShaderCache` compiles an entry point per definition set, so a shader-def
 *  branch is only ever compiled when a pipeline asks for it — which is how the
 *  `MORPH_TARGETS` branch of `pbr/mesh.slang` kept the HLSL `SV_VertexID` for so
 *  long: it makes Slang read BaseVertex and declare `OpCapability
 *  DrawParameters`, a capability naga rejects with "unsupported capability
 *  DrawParameters", and no test or example compiled that variant. Everything
 *  above this test passes with the shader broken. The fix is
 *  `SV_VulkanVertexID`; this test is what would catch a regression, so it drives
 *  the same `MeshPipeline::specialize` path the renderer uses and waits for the
 *  pipelines to become ready. */
TEST(MeshPipeline, SpecializedDescriptorsCompileOnADevice) {
    // The same asset and shader setup the asset-level tests above use: the
    // embedded source has to be reachable when the shader modules are loaded.
    App app = make_shader_asset_app();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(epix::camera::CameraPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::mesh::MeshPlugin{});

    // The shaders are published before the render plugin installs the pipeline
    // server, so the modules are registered by the time anything compiles them.
    pbr::register_mesh_shaders(app.world_mut());
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }
    flush_load_tasks(app);

    const auto& handles = app.resource<pbr::MeshShaderHandles>();
    // The view layouts have to agree with the pipeline's capability flags: a
    // layout carrying binding arrays while the pipeline says they are unusable
    // makes wgpu reject the pipeline for exceeding
    // `maxBindingArrayElementsPerShaderStage`, and the shader branches that only
    // a binding-array device takes are ones no real run compiles.
    const auto layouts = pbr::generate_view_layouts(wgpu::BufferBindingType::eStorage, wgpu::BufferBindingType::eStorage,
                                                    /*binding_arrays_are_usable=*/false, /*decals_feature_enabled=*/false,
                                                    /*partially_bound_binding_array_supported=*/false, /*oit_supported=*/false);
    wgpu::Limits limits{};
    limits.maxStorageBuffersPerShaderStage = 8;
    const auto pipeline                    = pbr::MeshPipeline::make(
        limits, wgpu::BufferBindingType::eStorage, handles.mesh, pbr::MeshPipelineViewLayouts{layouts}, false, false);

    // A real mesh's layout, so the vertex-aware definitions are the ones a
    // renderer would ask for.
    mesh::MeshVertexBufferLayouts mesh_layouts;
    const auto source = mesh::make_box2d(1.0f, 1.0f);
    const auto layout = source.get_mesh_vertex_buffer_layout(mesh_layouts);
    ASSERT_TRUE(layout.value);

    auto& server = app.resource<render::PipelineServer>();
    const auto compile = [&app, &server, &pipeline, &layout](pbr::MeshPipelineKey key, std::string_view name) {
        auto descriptor = pipeline.specialize(key, layout);
        ASSERT_TRUE(descriptor.has_value()) << name;
        const auto id = server.queue_render_pipeline(std::move(*descriptor));
        for (int attempt = 0; attempt < 400 && !server.get_render_pipeline(id).has_value(); ++attempt) {
            app.run_schedule(Last);
            std::this_thread::yield();
        }
        EXPECT_TRUE(server.get_render_pipeline(id).has_value())
            << name
            << ": the specialized pipeline never became ready. A shader-def branch that compiles to a module the "
               "backend rejects (an unsupported SPIR-V capability, for instance) fails here and nowhere else.";
    };

    const auto key_with = [](std::initializer_list<pbr::MeshPipelineKey> bits) {
        pbr::MeshPipelineKey key = pbr::MeshPipelineKey::NONE;
        for (const auto& bit : bits) key = key | bit;
        return key;
    };
    compile(key_with({}), "plain");
    compile(key_with({pbr::MeshPipelineKey::MORPH_TARGETS}), "MORPH_TARGETS");
    compile(key_with({pbr::MeshPipelineKey::MORPH_TARGETS, pbr::MeshPipelineKey::MOTION_VECTOR_PREPASS}),
            "MORPH_TARGETS+MOTION_VECTOR_PREPASS");
}