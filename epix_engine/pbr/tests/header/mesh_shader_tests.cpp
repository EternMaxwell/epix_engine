#include <epix/app.hpp>
#include <epix/assets.hpp>
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

    // The handles resource holds the three loaded modules.
    const auto& handles = app.resource<pbr::MeshShaderHandles>();
    auto& shaders       = app.resource<Assets<Shader>>();
    const auto types    = shaders.get(handles.mesh_types.id());
    const auto bindings = shaders.get(handles.mesh_bindings.id());
    const auto io       = shaders.get(handles.forward_io.id());
    ASSERT_TRUE(types.has_value());
    ASSERT_TRUE(bindings.has_value());
    ASSERT_TRUE(io.has_value());

    // `mesh_types` and `forward_io` are leaves; `mesh_bindings` declares exactly
    // one import, and it is a custom module name rather than a file path, which
    // is what `import epix.mesh_types;` has to resolve to.
    EXPECT_TRUE(types->get().imports.empty());
    EXPECT_TRUE(io->get().imports.empty());
    ASSERT_EQ(bindings->get().imports.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<std::filesystem::path>(bindings->get().imports[0].data));

    // Every module names itself, so a material shader can import it by name.
    EXPECT_TRUE(std::holds_alternative<std::filesystem::path>(types->get().import_path.data));
    EXPECT_TRUE(std::holds_alternative<std::filesystem::path>(bindings->get().import_path.data));
    EXPECT_TRUE(std::holds_alternative<std::filesystem::path>(io->get().import_path.data));
}
