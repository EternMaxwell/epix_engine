#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/render/maths.hpp>
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

namespace render = epix::render;

namespace {

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
        app.run_schedule(PreUpdate);
        app.run_schedule(PostUpdate);
        app.run_schedule(Last);
        std::this_thread::yield();
    }
}

}  // namespace

/** @brief The shared maths shader library registers through the embedded
 *  registry and loads through the asset server (Bevy
 *  `load_shader_library!(app, "maths.wgsl")`, `bevy_render/src/lib.rs:402`).
 *
 *  The mesh shaders import it for the affine and packed-normal helpers, so this
 *  is the module that makes `import epix.maths;` resolvable. */
TEST(MathsShader, RegistersAndLoadsThroughAssetServer) {
    auto app = make_shader_asset_app();

    render::register_maths_shader(app.world_mut());
    flush_load_tasks(app);

    auto& server = app.resource<AssetServer>();
    const std::string asset_path = std::string("embedded://") + std::string(render::MATHS_SHADER_PATH);
    auto handle                  = server.get_handle<Shader>(AssetPath(asset_path));
    ASSERT_TRUE(handle.has_value());
    EXPECT_TRUE(server.is_loaded_with_dependencies(handle->id())) << asset_path << " failed to load";

    auto& shaders = app.resource<Assets<Shader>>();
    auto shader   = shaders.get(handle->id());
    ASSERT_TRUE(shader.has_value());
    EXPECT_EQ(shader->get().path, asset_path);
    // The library imports nothing itself; everything in it is a leaf function.
    EXPECT_TRUE(shader->get().imports.empty());
    EXPECT_TRUE(std::holds_alternative<std::filesystem::path>(shader->get().import_path.data));
}
