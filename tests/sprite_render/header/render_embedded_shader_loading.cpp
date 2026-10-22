#include <gtest/gtest.h>

#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/image.hpp>
#include <epix/shader.hpp>
#include <epix/sprite_render.hpp>
#include <epix/task.hpp>

using namespace epix::app;
using namespace epix::assets;
using namespace epix::shader;

namespace sprite_render = epix::sprite_render;

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

    // The sprite-render plugins' slice computation reads `Assets<Image>` in PostUpdate,
    // which is a schedule this file now drives (Bevy flushes asset events there), so a
    // real app's image plugin has to be present for those systems to validate.
    app.add_plugins(epix::image::ImagePlugin{});

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

void expect_embedded_shader_loaded(App& app, std::string_view asset_path) {
    auto& server = app.resource<AssetServer>();
    auto handle  = server.get_handle<Shader>(AssetPath(std::string(asset_path)));
    ASSERT_TRUE(handle.has_value()) << asset_path;
    ASSERT_TRUE(handle->path().has_value()) << asset_path;
    ASSERT_TRUE(handle->path()->source.as_str().has_value()) << asset_path;
    EXPECT_EQ(handle->path()->source.as_str().value(), AssetPath(std::string(asset_path)).source.as_str().value());
    EXPECT_EQ(handle->path()->path.generic_string(), AssetPath(std::string(asset_path)).path.generic_string());
    EXPECT_TRUE(server.is_loaded_with_dependencies(handle->id())) << asset_path;

    auto& shaders = app.resource<Assets<Shader>>();
    auto shader   = shaders.get(handle->id());
    ASSERT_TRUE(shader.has_value()) << asset_path;
    EXPECT_EQ(shader->get().path, asset_path);
}

}  // namespace

TEST(Mesh2dRenderPlugin, RegistersAndLoadsEmbeddedShadersThroughAssetServer) {
    auto app = make_shader_asset_app();

    app.add_plugins(sprite_render::Mesh2dRenderPlugin{});
    flush_load_tasks(app);

    // Custom materials import the same types, bindings, transforms and view
    // interface as the stock shader; none is private to that one entry point.
    expect_embedded_shader_loaded(app, "embedded://sprite/mesh2d.slang");
    expect_embedded_shader_loaded(app, "embedded://sprite/mesh2d_vertex_output.slang");
    expect_embedded_shader_loaded(app, "embedded://sprite/mesh2d_view_bindings.slang");
    expect_embedded_shader_loaded(app, "embedded://sprite/mesh2d_types.slang");
    // Native loads this device-dependent library only in RenderStartup. With
    // no render app, publish its bytes without locking in storage-only defaults.
    EXPECT_TRUE(app.resource<EmbeddedAssetRegistry>().directory().get_file("sprite/mesh2d_bindings.slang"));
    EXPECT_FALSE(app.resource<AssetServer>().get_handle<Shader>(AssetPath{"embedded://sprite/mesh2d_bindings.slang"}));
    expect_embedded_shader_loaded(app, "embedded://sprite/mesh2d_functions.slang");
}

TEST(SpriteRenderPlugin, RegistersAndLoadsEmbeddedShadersThroughAssetServer) {
    auto app = make_shader_asset_app();

    app.add_plugins(sprite_render::SpriteRenderPlugin{});
    flush_load_tasks(app);

    expect_embedded_shader_loaded(app, "embedded://sprite/sprite.slang");
}
