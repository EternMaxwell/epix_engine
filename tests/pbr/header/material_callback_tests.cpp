#include <gtest/gtest.h>

#include <algorithm>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/pbr/material_plugin.hpp>
#include <epix/pbr/pbr_plugin.hpp>
#include <epix/pbr/prepass.hpp>
#include <epix/render.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <exception>
#include <expected>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

namespace {
namespace assets = epix::assets;
namespace ecs    = epix::ecs;
namespace mesh   = epix::mesh;
namespace pbr    = epix::pbr;
namespace render = epix::render;

struct CallbackMaterial : pbr::Material<CallbackMaterial> {
    int value                                                     = 7;
    static inline int calls                                       = 0;
    static inline int data                                        = 0;
    static inline const pbr::MaterialPipeline* seen_pipeline      = nullptr;
    static inline const mesh::MeshVertexBufferLayout* seen_layout = nullptr;
    static inline pbr::MeshPipelineKey seen_mesh_key{};
    static inline bool saw_material_layout = false;
    static inline bool saw_material_define = false;
    static inline bool saw_bindless_define = false;
    static std::expected<void, render::SpecializedMeshPipelineError> specialize(
        const pbr::MaterialPipeline&,
        render::RenderPipelineDescriptor&,
        const mesh::MeshVertexBufferLayoutRef&,
        pbr::MaterialPipelineKey<CallbackMaterial>);
};
}  // namespace

template <>
struct epix::render::render_resource::AsBindGroup<CallbackMaterial> : epix::render::render_resource::AsBindGroupBase {
    using Data  = int;
    using Param = std::tuple<>;
    static constexpr std::string_view label() { return "callback material"; }
    static std::vector<wgpu::BindGroupLayoutEntry> layout_entries() { return {}; }
    static Data bind_group_data(const CallbackMaterial& material) { return material.value; }
    static std::expected<epix::render::render_resource::UnpreparedBindGroup,
                         epix::render::render_resource::AsBindGroupError>
    unprepared_bind_group(const wgpu::Device&, const wgpu::BindGroupLayout&, const CallbackMaterial&, Param&) {
        return epix::render::render_resource::UnpreparedBindGroup{};
    }
};

namespace {
std::expected<void, render::SpecializedMeshPipelineError> CallbackMaterial::specialize(
    const pbr::MaterialPipeline& pipeline,
    render::RenderPipelineDescriptor& descriptor,
    const mesh::MeshVertexBufferLayoutRef& layout,
    pbr::MaterialPipelineKey<CallbackMaterial> key) {
    ++calls;
    data                = key.bind_group_data;
    seen_pipeline       = &pipeline;
    seen_layout         = layout.value.get();
    seen_mesh_key       = key.mesh_key;
    saw_material_layout = descriptor.layouts.size() == 4;
    saw_material_define = std::ranges::any_of(descriptor.vertex.shader_defs, [](const auto& def) {
        const auto* value = std::get_if<std::uint32_t>(&def.value);
        return def.name == "MATERIAL_BIND_GROUP" && value && *value == 3u;
    });
    saw_bindless_define =
        std::ranges::any_of(descriptor.vertex.shader_defs, [](const auto& def) { return def.name == "BINDLESS"; });
    if (data < 0)
        return std::unexpected(render::SpecializedMeshPipelineError{
            mesh::MissingVertexAttributeError{.pipeline_type = "callback material",
                                              .id            = mesh::MeshVertexAttributeId{707},
                                              .name          = "callback requested attribute"}});
    descriptor.label = "custom material callback " + std::to_string(data);
    return {};
}

static_assert(std::is_same_v<pbr::MaterialData<CallbackMaterial>::type, int>);

class MaterialCallback : public testing::Test {
   protected:
    epix::app::App app = epix::app::App::create();
    ecs::World* world  = nullptr;
    std::vector<assets::Handle<CallbackMaterial>> material_handles;

    void SetUp() override {
        CallbackMaterial::calls               = 0;
        CallbackMaterial::data                = 0;
        CallbackMaterial::seen_pipeline       = nullptr;
        CallbackMaterial::seen_layout         = nullptr;
        CallbackMaterial::saw_material_layout = false;
        CallbackMaterial::saw_material_define = false;
        CallbackMaterial::saw_bindless_define = false;
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPoolBuilder{}.num_threads(4).build());
        assets::AssetPlugin asset_plugin;
        asset_plugin.mode = assets::AssetServerMode::Unprocessed;
        app.add_events<epix::window::WindowClosed>();
        app.add_plugins(epix::time::TimePlugin{}, epix::camera::CameraPlugin{}, std::move(asset_plugin),
                        epix::image::ImagePlugin{}, mesh::MeshPlugin{}, epix::transform::TransformPlugin{},
                        render::FrameCountPlugin{});
        try {
            app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
        } catch (const std::exception& error) {
            GTEST_SKIP() << "GPU/Vulkan unavailable: " << error.what();
        }
        app.add_plugins(epix::core_graph::CoreGraphPlugin{}, pbr::MaterialPlugin<CallbackMaterial>{},
                        pbr::PbrPlugin{.supports_storage_buffers = true});
        app.resource_scope([&](epix::app::internal::Plugins& plugins) { plugins.ready_all(app); });
        auto& render_app = app.get_sub_app_mut(render::Render)->get();
        world            = &render_app.world_mut();
        world->insert_resource(pbr::DefaultOpaqueRendererMethod{});
        render_app.run_schedule(render::RenderStartup);
    }

    std::optional<pbr::PreparedMaterial> prepare(CallbackMaterial material = {}) {
        using Asset = render::erased_render_asset::ErasedRenderAsset<pbr::MeshMaterial3d<CallbackMaterial>>;
        const auto id =
            material_handles.emplace_back(app.resource_mut<assets::Assets<CallbackMaterial>>().emplace(material)).id();
        std::optional<pbr::PreparedMaterial> prepared;
        auto system = ecs::make_system_unique([&](Asset::Param params) {
            auto result = Asset::prepare_asset(std::move(material), id, params, nullptr);
            EXPECT_TRUE(result.has_value());
            if (result) prepared = std::move(*result);
        });
        system->initialize(*world);
        EXPECT_TRUE(system->run({}, *world).has_value());
        system->apply_deferred(*world);
        return prepared;
    }

    static mesh::MeshVertexBufferLayoutRef cube_layout() {
        mesh::MeshVertexBufferLayouts layouts;
        return mesh::Cuboid(1.0f, 1.0f, 1.0f).mesh().build().get_mesh_vertex_buffer_layout(layouts);
    }
    static pbr::ErasedMaterialPipelineKey key_for(const pbr::PreparedMaterial& material) {
        return {.mesh_key     = material.properties->mesh_pipeline_key_bits,
                .material_key = material.properties->material_key,
                .type_id      = epix::meta::type_id<CallbackMaterial>()};
    }
};

TEST_F(MaterialCallback, PreparationRetainsTheMaterialSpecializationCallback) {
    const auto prepared = prepare();
    ASSERT_TRUE(prepared);
    ASSERT_NE(prepared->properties->specialize, nullptr);
    const auto layout = cube_layout();
    const pbr::MaterialPipelineSpecializer pipeline{world->resource<pbr::MaterialPipeline>(), prepared->properties};
    const auto key        = key_for(*prepared);
    const auto descriptor = pipeline.specialize(key, layout);
    ASSERT_TRUE(descriptor);
    EXPECT_EQ(CallbackMaterial::calls, 1);
    EXPECT_EQ(CallbackMaterial::data, 7);
    EXPECT_EQ(CallbackMaterial::seen_pipeline, &pipeline.pipeline);
    EXPECT_EQ(CallbackMaterial::seen_layout, layout.value.get());
    EXPECT_EQ(CallbackMaterial::seen_mesh_key, key.mesh_key);
    EXPECT_TRUE(CallbackMaterial::saw_material_layout);
    EXPECT_TRUE(CallbackMaterial::saw_material_define);
    EXPECT_EQ(descriptor->label, "custom material callback 7");
}

TEST_F(MaterialCallback, CallbackErrorsArePropagatedAndNotCached) {
    const auto prepared = prepare(CallbackMaterial{.value = -1});
    ASSERT_TRUE(prepared);
    auto& cache = world->resource_mut<render::SpecializedMeshPipelines<pbr::MaterialPipelineSpecializer>>();
    const pbr::MaterialPipelineSpecializer pipeline{world->resource<pbr::MaterialPipeline>(), prepared->properties};
    const auto key    = key_for(*prepared);
    const auto layout = cube_layout();
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto result = cache.specialize(world->resource<render::PipelineServer>(), pipeline, key, layout);
        ASSERT_FALSE(result);
        const auto& error = std::get<mesh::MissingVertexAttributeError>(result.error());
        EXPECT_EQ(error.id.value, 707u);
        EXPECT_EQ(error.name, "callback requested attribute");
        ASSERT_TRUE(error.pipeline_type);
        EXPECT_EQ(*error.pipeline_type, epix::meta::type_id<pbr::MaterialPipelineSpecializer>().short_name());
    }
    EXPECT_EQ(CallbackMaterial::calls, 2);
}

TEST_F(MaterialCallback, AssociatedDataSeparatesPipelinesAndCacheHitsReuseThem) {
    const auto first  = prepare(CallbackMaterial{.value = 1});
    const auto second = prepare(CallbackMaterial{.value = 2});
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    auto& cache        = world->resource_mut<render::SpecializedMeshPipelines<pbr::MaterialPipelineSpecializer>>();
    const auto& server = world->resource<render::PipelineServer>();
    const pbr::MaterialPipelineSpecializer a{world->resource<pbr::MaterialPipeline>(), first->properties};
    const pbr::MaterialPipelineSpecializer b{world->resource<pbr::MaterialPipeline>(), second->properties};
    const auto layout    = cube_layout();
    const auto first_id  = cache.specialize(server, a, key_for(*first), layout);
    const auto second_id = cache.specialize(server, b, key_for(*second), layout);
    ASSERT_TRUE(first_id);
    ASSERT_TRUE(second_id);
    EXPECT_NE(*first_id, *second_id);
    EXPECT_EQ(CallbackMaterial::calls, 2);
    const auto reused_id = cache.specialize(server, a, key_for(*first), layout);
    ASSERT_TRUE(reused_id);
    EXPECT_EQ(*reused_id, *first_id);
    EXPECT_EQ(CallbackMaterial::calls, 2);
}

TEST_F(MaterialCallback, CallbackRunsBeforeTheBindlessDefineIsAdded) {
    const auto prepared = prepare();
    ASSERT_TRUE(prepared);
    // Exercise descriptor ordering, not a claim of end-to-end bindless support.
    // The typed callback itself still comes from actual material preparation.
    prepared->properties->bindless = true;
    const pbr::MaterialPipelineSpecializer pipeline{world->resource<pbr::MaterialPipeline>(), prepared->properties};
    const auto result = pipeline.specialize(key_for(*prepared), cube_layout());
    ASSERT_TRUE(result);
    EXPECT_FALSE(CallbackMaterial::saw_bindless_define);
    EXPECT_TRUE(
        std::ranges::any_of(result->vertex.shader_defs, [](const auto& def) { return def.name == "BINDLESS"; }));
    ASSERT_TRUE(result->fragment);
    EXPECT_TRUE(
        std::ranges::any_of(result->fragment->shader_defs, [](const auto& def) { return def.name == "BINDLESS"; }));
}

TEST_F(MaterialCallback, PrepassCallbackUsesOwnedPipelineOriginalKeyAndExistingBindlessDefine) {
    const auto prepared = prepare(CallbackMaterial{.value = 23});
    ASSERT_TRUE(prepared);
    prepared->properties->bindless = true;
    const pbr::PrepassPipelineSpecializer pipeline{world->resource<pbr::PrepassPipeline>(), prepared->properties};
    auto key = key_for(*prepared);
    key.mesh_key = key.mesh_key | pbr::MeshPipelineKey::NORMAL_PREPASS | pbr::MeshPipelineKey::MOTION_VECTOR_PREPASS;
    const auto layout = cube_layout();
    const auto descriptor = pipeline.specialize(key, layout);
    ASSERT_TRUE(descriptor);
    EXPECT_EQ(CallbackMaterial::calls, 1);
    EXPECT_EQ(CallbackMaterial::data, 23);
    EXPECT_EQ(CallbackMaterial::seen_mesh_key, key.mesh_key);
    EXPECT_EQ(CallbackMaterial::seen_layout, layout.value.get());
    EXPECT_EQ(CallbackMaterial::seen_pipeline, &pipeline.pipeline.material_pipeline);
    EXPECT_NE(CallbackMaterial::seen_pipeline, &world->resource<pbr::MaterialPipeline>());
    EXPECT_TRUE(CallbackMaterial::saw_material_layout);
    EXPECT_TRUE(CallbackMaterial::saw_material_define);
    EXPECT_TRUE(CallbackMaterial::saw_bindless_define) << "native prepass ordering differs from forward";
    EXPECT_EQ(descriptor->label, "custom material callback 23");
    EXPECT_EQ(descriptor->vertex.shader, pipeline.pipeline.default_prepass_shader);
    EXPECT_TRUE(std::ranges::any_of(descriptor->vertex.shader_defs,
        [](const auto& def) { return def.name == "PREPASS_PIPELINE"; }));
    ASSERT_TRUE(descriptor->fragment);
    EXPECT_EQ(descriptor->fragment->shader, pipeline.pipeline.default_prepass_shader);
    EXPECT_TRUE(std::ranges::any_of(descriptor->fragment->shader_defs,
        [](const auto& def) { return def.name == "BINDLESS"; }));
}

TEST_F(MaterialCallback, PrepassCallbackErrorsPropagateWithoutCaching) {
    const auto prepared = prepare(CallbackMaterial{.value = -1});
    ASSERT_TRUE(prepared);
    auto& cache = world->resource_mut<render::SpecializedMeshPipelines<pbr::PrepassPipelineSpecializer>>();
    const pbr::PrepassPipelineSpecializer pipeline{world->resource<pbr::PrepassPipeline>(), prepared->properties};
    const auto key = key_for(*prepared);
    const auto layout = cube_layout();
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        const auto result = cache.specialize(world->resource<render::PipelineServer>(), pipeline, key, layout);
        ASSERT_FALSE(result);
        const auto& error = std::get<mesh::MissingVertexAttributeError>(result.error());
        EXPECT_EQ(error.id.value, 707u);
        EXPECT_EQ(error.name, "callback requested attribute");
        ASSERT_TRUE(error.pipeline_type);
        EXPECT_EQ(*error.pipeline_type, epix::meta::type_id<pbr::PrepassPipelineSpecializer>().short_name());
    }
    EXPECT_EQ(CallbackMaterial::calls, 2);
}

TEST_F(MaterialCallback, PrepassAssociatedDataSeparatesPipelinesAndHitsReuseThem) {
    const auto first = prepare(CallbackMaterial{.value = 1});
    const auto second = prepare(CallbackMaterial{.value = 2});
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    auto& cache = world->resource_mut<render::SpecializedMeshPipelines<pbr::PrepassPipelineSpecializer>>();
    const auto& server = world->resource<render::PipelineServer>();
    const pbr::PrepassPipelineSpecializer a{world->resource<pbr::PrepassPipeline>(), first->properties};
    const pbr::PrepassPipelineSpecializer b{world->resource<pbr::PrepassPipeline>(), second->properties};
    const auto layout = cube_layout();
    const auto first_id = cache.specialize(server, a, key_for(*first), layout);
    const auto second_id = cache.specialize(server, b, key_for(*second), layout);
    ASSERT_TRUE(first_id);
    ASSERT_TRUE(second_id);
    EXPECT_NE(*first_id, *second_id);
    EXPECT_EQ(CallbackMaterial::calls, 2);
    const auto reused = cache.specialize(server, a, key_for(*first), layout);
    ASSERT_TRUE(reused);
    EXPECT_EQ(*reused, *first_id);
    EXPECT_EQ(CallbackMaterial::calls, 2);
}

TEST_F(MaterialCallback, MissingPrepassMaterialLayoutFailsBeforeCallingTheHook) {
    const auto prepared = prepare();
    ASSERT_TRUE(prepared);
    prepared->properties->material_layout.reset();
    const pbr::PrepassPipelineSpecializer pipeline{world->resource<pbr::PrepassPipeline>(), prepared->properties};
    EXPECT_THROW((void)pipeline.specialize(key_for(*prepared), cube_layout()), std::bad_optional_access);
    EXPECT_EQ(CallbackMaterial::calls, 0);
}

TEST(MaterialKey, TypedRecoveryClonesTheValueAndRejectsAnIncorrectType) {
    const pbr::ErasedMaterialKey key{std::string{"original"}};
    auto copy = key.to_key<std::string>();
    copy      = "changed";
    EXPECT_EQ(key.to_key<std::string>(), "original");
    EXPECT_THROW(key.to_key<int>(), std::bad_cast);
    EXPECT_EQ(pbr::ErasedMaterialKey{}.to_key<std::monostate>(), std::monostate{});
}
}  // namespace
