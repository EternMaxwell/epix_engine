#include <gtest/gtest.h>

#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/task.hpp>

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
    asset_plugin.attach(app);

    ShaderPlugin shader_plugin;
    shader_plugin.attach(app);

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

// Bevy's `GlobalsPlugin` loads the `globals` shader library with
// `load_shader_library!(app, "globals.wgsl")` (bevy_render/src/globals.rs), and
// mesh2d's view bindings import `bevy_render::globals::Globals`. Epix registers
// the same library as a Slang module so the mesh2d shader can import it.
TEST(GlobalsPlugin, RegistersAndLoadsTheGlobalsShaderLibrary) {
    auto app = make_shader_asset_app();

    app.add_plugins(render::GlobalsPlugin{});
    flush_load_tasks(app);

    expect_embedded_shader_loaded(app, "embedded://epix/shaders/globals.slang");
}
