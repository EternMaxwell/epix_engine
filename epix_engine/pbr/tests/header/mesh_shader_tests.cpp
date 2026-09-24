#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/pbr/mesh_pipeline_resource.hpp>
#include <epix/pbr/mesh_shaders.hpp>
#include <epix/shader.hpp>
#include <epix/task.hpp>
#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <variant>

using namespace epix::app;
using namespace epix::assets;
using namespace epix::shader;

namespace pbr = epix::pbr;

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

    // The handles resource holds every loaded module.
    const auto& handles = app.resource<pbr::MeshShaderHandles>();
    auto& shaders       = app.resource<Assets<Shader>>();
    const auto types    = shaders.get(handles.mesh_types.id());
    const auto bindings = shaders.get(handles.mesh_bindings.id());
    const auto io       = shaders.get(handles.forward_io.id());
    const auto view_types       = shaders.get(handles.mesh_view_types.id());
    const auto atmosphere_types = shaders.get(handles.atmosphere_types.id());
    ASSERT_TRUE(types.has_value());
    ASSERT_TRUE(bindings.has_value());
    ASSERT_TRUE(io.has_value());
    ASSERT_TRUE(view_types.has_value());
    ASSERT_TRUE(atmosphere_types.has_value());

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
