#include <gtest/gtest.h>

#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/pbr/material_plugin.hpp>
#include <epix/pbr/pbr_plugin.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>

#include <array>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace {
namespace assets = epix::assets;
namespace ecs = epix::ecs;
namespace pbr = epix::pbr;
namespace render = epix::render;
namespace shader = epix::shader;

struct IoTaskPoolInit {
    IoTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPool{epix::task::TaskPoolBuilder{}.num_threads(4)});
    }
} io_task_pool_init;

struct ShaderMaterial : pbr::Material<ShaderMaterial> {
    static inline std::array<shader::ShaderRef, 6> refs{};
    static shader::ShaderRef vertex_shader() { return refs[0]; }
    static shader::ShaderRef fragment_shader() { return refs[1]; }
    static shader::ShaderRef prepass_vertex_shader() { return refs[2]; }
    static shader::ShaderRef prepass_fragment_shader() { return refs[3]; }
    static shader::ShaderRef deferred_vertex_shader() { return refs[4]; }
    static shader::ShaderRef deferred_fragment_shader() { return refs[5]; }
};
}  // namespace

template <>
struct epix::render::render_resource::AsBindGroup<ShaderMaterial>
    : epix::render::render_resource::AsBindGroupBase {
    using Data = std::monostate;
    using Param = std::tuple<>;
    static constexpr std::string_view label() { return "material shader test"; }
    static std::vector<wgpu::BindGroupLayoutEntry> layout_entries() { return {}; }
    static Data bind_group_data(const ShaderMaterial&) { return {}; }
    static std::expected<epix::render::render_resource::UnpreparedBindGroup,
                         epix::render::render_resource::AsBindGroupError>
    unprepared_bind_group(const wgpu::Device&, const wgpu::BindGroupLayout&, const ShaderMaterial&, Param&) {
        return epix::render::render_resource::UnpreparedBindGroup{};
    }
};

namespace {
class MaterialShaders : public testing::Test {
   protected:
    epix::app::App app = epix::app::App::create();
    ecs::World* world = nullptr;
    static constexpr std::string_view shader_text =
        "[shader(\"fragment\")] float4 fragment() : SV_Target { return float4(0.0, 1.0, 0.0, 1.0); }";

    void SetUp() override {
        ShaderMaterial::refs = {};
        auto directory = assets::memory::Directory::create({});
        const auto bytes = std::as_bytes(std::span{shader_text});
        const auto data = std::make_shared<std::vector<std::byte>>(bytes.begin(), bytes.end());
        for (int index = 0; index < 6; ++index)
            ASSERT_TRUE(directory.insert_file("shaders/ref" + std::to_string(index) + ".slang",
                                               assets::memory::Value::from_shared(data)));
        const auto source = [&] {
            return assets::AssetSourceBuilder::create([directory]() -> std::unique_ptr<assets::AssetReader> {
                return std::make_unique<assets::MemoryAssetReader>(directory);
            });
        };
        assets::AssetPlugin asset_plugin;
        asset_plugin.mode = assets::AssetServerMode::Unprocessed;
        asset_plugin.register_asset_source(assets::AssetSourceId{}, source());
        asset_plugin.register_asset_source(assets::AssetSourceId{std::string{"pbr66"}}, source());
        asset_plugin.register_asset_source(assets::AssetSourceId{std::string{"x"}}, source());
        app.add_events<epix::window::WindowClosed>();
        app.add_plugins(epix::time::TimePlugin{}, epix::camera::CameraPlugin{}, std::move(asset_plugin),
                        epix::image::ImagePlugin{}, epix::mesh::MeshPlugin{}, epix::transform::TransformPlugin{},
                        shader::ShaderPlugin{}, render::FrameCountPlugin{});
        try {
            app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
        } catch (const std::exception& error) {
            GTEST_SKIP() << "GPU/Vulkan unavailable: " << error.what();
        }
        app.add_plugins(epix::core_graph::CoreGraphPlugin{}, pbr::MaterialPlugin<ShaderMaterial>{},
                        pbr::PbrPlugin{.supports_storage_buffers = true});
        app.resource_scope([&](epix::app::internal::Plugins& plugins) { plugins.ready_all(app); });
        auto& render_app = app.get_sub_app_mut(render::Render)->get();
        world = &render_app.world_mut();
        world->insert_resource(pbr::DefaultOpaqueRendererMethod{});
        render_app.run_schedule(render::RenderStartup);
    }

    void TearDown() override { ShaderMaterial::refs = {}; }

    std::optional<pbr::PreparedMaterial> prepare() {
        using Asset = render::erased_render_asset::ErasedRenderAsset<pbr::MeshMaterial3d<ShaderMaterial>>;
        std::optional<pbr::PreparedMaterial> prepared;
        auto system = ecs::make_system_unique([&](Asset::Param params) {
            auto result = Asset::prepare_asset(ShaderMaterial{}, assets::AssetId<ShaderMaterial>::invalid(), params, nullptr);
            EXPECT_TRUE(result.has_value());
            if (result) prepared = std::move(*result);
        });
        system->initialize(*world);
        EXPECT_TRUE(system->run({}, *world).has_value());
        system->apply_deferred(*world);
        return prepared;
    }

    void wait_for(const auto& handles) {
        const auto& server = app.resource<assets::AssetServer>();
        const auto loaded = [&] {
            return std::ranges::all_of(handles, [&](const auto& handle) {
                return server.is_loaded_with_dependencies(handle.id());
            });
        };
        for (int attempt = 0; attempt < 10000 && !loaded(); ++attempt) {
            app.run_schedule(epix::app::PreUpdate);
            app.run_schedule(epix::app::PostUpdate);
            app.run_schedule(epix::app::Last);
            std::this_thread::yield();
        }
        ASSERT_TRUE(loaded());
    }
};

TEST_F(MaterialShaders, DefaultReferencesLeaveTheOverrideListEmpty) {
    const auto prepared = prepare();
    ASSERT_TRUE(prepared);
    EXPECT_TRUE(prepared->properties->shaders.empty());
}

TEST_F(MaterialShaders, HandleReferencesReachThePreparedMaterial) {
    std::vector<assets::Handle<shader::Shader>> expected;
    auto& shaders = app.resource_mut<assets::Assets<shader::Shader>>();
    for (int index = 0; index < 6; ++index) {
        expected.push_back(shaders.emplace(shader::Shader::from_slang(
            std::string{shader_text}, assets::AssetPath{"manual/ref" + std::to_string(index) + ".slang"})));
        ShaderMaterial::refs[index] = shader::ShaderRef::from_handle(expected.back());
    }
    const auto prepared = prepare();
    ASSERT_TRUE(prepared);
    const auto& properties = *prepared->properties;
    ASSERT_EQ(properties.shaders.size(), 6u);
    for (std::size_t index = 0; index < expected.size(); ++index)
        EXPECT_EQ(properties.shaders[index].second, expected[index]);
    ASSERT_NE(properties.get_shader<pbr::MaterialVertexShader>(), nullptr);
    EXPECT_EQ(*properties.get_shader<pbr::MaterialVertexShader>(), expected[0]);
    ASSERT_NE(properties.get_shader<pbr::MaterialFragmentShader>(), nullptr);
    EXPECT_EQ(*properties.get_shader<pbr::MaterialFragmentShader>(), expected[1]);
    ASSERT_NE(properties.get_shader<pbr::PrepassVertexShader>(), nullptr);
    EXPECT_EQ(*properties.get_shader<pbr::PrepassVertexShader>(), expected[2]);
    ASSERT_NE(properties.get_shader<pbr::PrepassFragmentShader>(), nullptr);
    EXPECT_EQ(*properties.get_shader<pbr::PrepassFragmentShader>(), expected[3]);
    ASSERT_NE(properties.get_shader<pbr::DeferredVertexShader>(), nullptr);
    EXPECT_EQ(*properties.get_shader<pbr::DeferredVertexShader>(), expected[4]);
    ASSERT_NE(properties.get_shader<pbr::DeferredFragmentShader>(), nullptr);
    EXPECT_EQ(*properties.get_shader<pbr::DeferredFragmentShader>(), expected[5]);
}

TEST_F(MaterialShaders, PathsUseDefaultNamedAndEmbeddedAssetSourcesWithoutRewriting) {
    app.resource_mut<assets::EmbeddedAssetRegistry>().insert_asset_static(
        "tests/pbr66/ref.slang", std::as_bytes(std::span{shader_text}));
    const std::array<std::string, 6> paths{"shaders/ref0.slang", "pbr66://shaders/ref1.slang",
                                         "embedded://tests/pbr66/ref.slang", "x://shaders/ref3.slang",
                                         "shaders/ref4.slang", "embedded://tests/pbr66/ref.slang"};
    std::vector<assets::Handle<shader::Shader>> expected;
    for (std::size_t index = 0; index < paths.size(); ++index) {
        ShaderMaterial::refs[index] = shader::ShaderRef::from_str(paths[index]);
        expected.push_back(app.resource<assets::AssetServer>().load<shader::Shader>(paths[index]));
    }
    const auto prepared = prepare();
    ASSERT_TRUE(prepared);
    ASSERT_EQ(prepared->properties->shaders.size(), 6u);
    for (std::size_t index = 0; index < paths.size(); ++index)
        EXPECT_EQ(prepared->properties->shaders[index].second, expected[index]) << paths[index];
    wait_for(expected);
}

TEST_F(MaterialShaders, RepreparationDropsDefaultsAndReplacesOldOverrides) {
    auto& shaders = app.resource_mut<assets::Assets<shader::Shader>>();
    const auto first_shader = shaders.emplace(shader::Shader::from_slang(
        std::string{shader_text}, assets::AssetPath{"manual/first.slang"}));
    const auto second_shader = shaders.emplace(shader::Shader::from_slang(
        std::string{shader_text}, assets::AssetPath{"manual/second.slang"}));
    ShaderMaterial::refs[0] = shader::ShaderRef::from_handle(first_shader);
    ShaderMaterial::refs[1] = shader::ShaderRef::from_handle(first_shader);
    const auto first = prepare();
    ASSERT_TRUE(first);
    ASSERT_EQ(first->properties->shaders.size(), 2u);
    ShaderMaterial::refs = {};
    ShaderMaterial::refs[1] = shader::ShaderRef::from_handle(second_shader);
    const auto second = prepare();
    ASSERT_TRUE(second);
    ASSERT_EQ(second->properties->shaders.size(), 1u);
    EXPECT_EQ(second->properties->get_shader<pbr::MaterialVertexShader>(), nullptr);
    ASSERT_NE(second->properties->get_shader<pbr::MaterialFragmentShader>(), nullptr);
    EXPECT_EQ(*second->properties->get_shader<pbr::MaterialFragmentShader>(), second_shader);
    EXPECT_EQ(*first->properties->get_shader<pbr::MaterialFragmentShader>(), first_shader);
}
}  // namespace
