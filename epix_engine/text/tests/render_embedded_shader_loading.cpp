#include <gtest/gtest.h>

import epix.assets;
import epix.core;
import epix.mesh;
import epix.shader;
import epix.sprite;
import epix.text;

using namespace epix::assets;
using namespace epix::core;
using namespace epix::shader;

namespace mesh   = epix::mesh;
namespace sprite = epix::sprite;
namespace text   = epix::text;

namespace {

App make_shader_asset_app() {
    App app = App::create();

    AssetPlugin asset_plugin;
    asset_plugin.mode = AssetServerMode::Unprocessed;
    asset_plugin.build(app);

    ShaderPlugin shader_plugin;
    shader_plugin.build(app);

    return app;
}

void flush_load_tasks(App& app) {
    epix::utils::IOTaskPool::instance().wait();
    app.run_schedule(Last);
    app.run_schedule(Last);
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

    mesh::MeshRenderPlugin plugin;
    plugin.build(app);
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
    plugin.build(app);
    flush_load_tasks(app);

    expect_embedded_shader_loaded(app, "embedded://sprite/sprite_vertex.slang");
    expect_embedded_shader_loaded(app, "embedded://sprite/sprite_fragment.slang");
}

TEST(TextRenderPlugin, Build_RegistersAndLoadsEmbeddedShadersThroughAssetServer) {
    auto app = make_shader_asset_app();

    text::TextRenderPlugin plugin;
    plugin.build(app);
    flush_load_tasks(app);

    expect_embedded_shader_loaded(app, "embedded://text/text_vertex.slang");
    expect_embedded_shader_loaded(app, "embedded://text/text_fragment.slang");
}