#include <gtest/gtest.h>

import epix.assets;
import epix.ecs;
import epix.app;
import epix.mesh;
import epix.sprite;
import epix.sprite_render;
import epix.shader;
import epix.task;
import epix.text;

using namespace epix::assets;
using namespace epix::ecs;
using namespace epix::app;
using namespace epix::shader;

namespace mesh   = epix::mesh;
namespace sprite = epix::sprite;
namespace text   = epix::text;

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

TEST(MeshRenderPlugin, Build_RegistersAndLoadsEmbeddedShadersThroughAssetServer) {
    auto app = make_shader_asset_app();

    sprite_render::Mesh2dRenderPlugin plugin;
    plugin.attach(app);
    flush_load_tasks(app);

    expect_embedded_shader_loaded(app, "embedded://mesh/solid_vertex.slang");
    expect_embedded_shader_loaded(app, "embedded://mesh/vertex_color_vertex.slang");
    expect_embedded_shader_loaded(app, "embedded://mesh/textured_vertex.slang");
    expect_embedded_shader_loaded(app, "embedded://mesh/textured_vertex_color_vertex.slang");
    expect_embedded_shader_loaded(app, "embedded://mesh/color_fragment.slang");
    expect_embedded_shader_loaded(app, "embedded://mesh/textured_fragment.slang");
}

TEST(SpritePlugin, Build_RegistersAndLoadsEmbeddedShadersThroughAssetServer) {
    auto app = make_shader_asset_app();

    sprite::SpritePlugin plugin;
    plugin.attach(app);
    flush_load_tasks(app);

    expect_embedded_shader_loaded(app, "embedded://sprite/sprite_vertex.slang");
    expect_embedded_shader_loaded(app, "embedded://sprite/sprite_fragment.slang");
}

TEST(TextRenderPlugin, Build_RegistersAndLoadsEmbeddedShadersThroughAssetServer) {
    auto app = make_shader_asset_app();

    text::TextRenderPlugin plugin;
    plugin.attach(app);
    flush_load_tasks(app);

    expect_embedded_shader_loaded(app, "embedded://text/text_vertex.slang");
    expect_embedded_shader_loaded(app, "embedded://text/text_fragment.slang");
}
