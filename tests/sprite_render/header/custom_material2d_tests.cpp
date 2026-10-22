#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/sprite_render.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <exception>
#include <expected>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

namespace {
namespace assets        = epix::assets;
namespace ecs           = epix::ecs;
namespace mesh          = epix::mesh;
namespace render        = epix::render;
namespace shader        = epix::shader;
namespace sprite_render = epix::sprite_render;

struct ConsumerMaterial2d : sprite_render::Material2d<ConsumerMaterial2d> {
    int variant = 7;
    static inline std::array<shader::ShaderRef, 2> references{};
    static inline int calls     = 0;
    static inline int seen_data = 0;
    static inline sprite_render::Mesh2dPipelineKey seen_mesh_key{};
    static inline const mesh::MeshVertexBufferLayout* seen_layout = nullptr;
    static inline bool saw_material_defines                       = false;
    static inline bool saw_three_layouts                          = false;
    static shader::ShaderRef vertex_shader() { return references[0]; }
    static shader::ShaderRef fragment_shader() { return references[1]; }
    static std::expected<void, render::SpecializedMeshPipelineError> specialize(
        render::RenderPipelineDescriptor&,
        const mesh::MeshVertexBufferLayoutRef&,
        sprite_render::Material2dKey<ConsumerMaterial2d>);
};
}  // namespace

template <>
struct epix::render::render_resource::AsBindGroup<ConsumerMaterial2d> : epix::render::render_resource::AsBindGroupBase {
    using Data  = int;
    using Param = std::tuple<>;
    static constexpr std::string_view label() { return "consumer material2d"; }
    static std::vector<wgpu::BindGroupLayoutEntry> layout_entries() { return {}; }
    static Data bind_group_data(const ConsumerMaterial2d& material) { return material.variant; }
    static std::expected<epix::render::render_resource::UnpreparedBindGroup,
                         epix::render::render_resource::AsBindGroupError>
    unprepared_bind_group(const wgpu::Device&, const wgpu::BindGroupLayout&, const ConsumerMaterial2d&, Param&) {
        return epix::render::render_resource::UnpreparedBindGroup{};
    }
};

namespace {
std::expected<void, render::SpecializedMeshPipelineError> ConsumerMaterial2d::specialize(
    render::RenderPipelineDescriptor& descriptor,
    const mesh::MeshVertexBufferLayoutRef& layout,
    sprite_render::Material2dKey<ConsumerMaterial2d> key) {
    ++calls;
    seen_data             = key.bind_group_data;
    seen_mesh_key         = key.mesh_key;
    seen_layout           = layout.value.get();
    const auto has_define = [](const auto& defs) {
        return std::ranges::any_of(defs, [](const auto& def) {
            const auto* value = std::get_if<std::uint32_t>(&def.value);
            return def.name == "MATERIAL_BIND_GROUP" && value && *value == 2u;
        });
    };
    saw_material_defines = has_define(descriptor.vertex.shader_defs) && descriptor.fragment &&
                           has_define(descriptor.fragment->shader_defs);
    saw_three_layouts    = descriptor.layouts.size() == 3;
    if (key.bind_group_data < 0)
        return std::unexpected(render::SpecializedMeshPipelineError{
            mesh::MissingVertexAttributeError{.pipeline_type = "consumer material2d",
                                              .id            = mesh::MeshVertexAttributeId{909},
                                              .name          = "callback requested attribute"}});
    descriptor.label = "consumer material2d " + std::to_string(key.bind_group_data);
    return {};
}

static_assert(std::is_same_v<sprite_render::Material2dData<ConsumerMaterial2d>::type, int>);

TEST(CustomMaterial2d, AConsumerDefinedMaterialCanInstallItsPluginWithoutARenderApp) {
    epix::task::IoTaskPool::get_or_init(epix::task::TaskPoolBuilder{}.num_threads(4).build());
    auto app = epix::app::App::create();
    assets::AssetPlugin asset_plugin;
    asset_plugin.mode = assets::AssetServerMode::Unprocessed;
    app.add_plugins(std::move(asset_plugin), sprite_render::Material2dPlugin<ConsumerMaterial2d>{});
    auto& materials   = app.resource_mut<assets::Assets<ConsumerMaterial2d>>();
    const auto handle = materials.emplace(ConsumerMaterial2d{});
    EXPECT_TRUE(materials.get(handle));
}

class Material2dPipeline : public testing::Test {
   protected:
    epix::app::App app = epix::app::App::create();
    ecs::World* world  = nullptr;
    virtual bool uniform_fallback() const { return false; }
    void check_transform_records();
    static constexpr std::string_view shader_text =
        "[shader(\"fragment\")] float4 fragment() : SV_Target { return float4(0.0, 1.0, 0.0, 1.0); }";

    void SetUp() override {
        ConsumerMaterial2d::references           = {};
        ConsumerMaterial2d::calls                = 0;
        ConsumerMaterial2d::seen_layout          = nullptr;
        ConsumerMaterial2d::saw_material_defines = false;
        ConsumerMaterial2d::saw_three_layouts    = false;
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPoolBuilder{}.num_threads(4).build());
        auto directory   = assets::memory::Directory::create({});
        const auto bytes = std::as_bytes(std::span{shader_text});
        const auto data  = std::make_shared<std::vector<std::byte>>(bytes.begin(), bytes.end());
        ASSERT_TRUE(directory.insert_file("shaders/custom.slang", assets::memory::Value::from_shared(data)));
        const auto source = [directory] {
            return assets::AssetSourceBuilder::create([directory]() -> std::unique_ptr<assets::AssetReader> {
                return std::make_unique<assets::MemoryAssetReader>(directory);
            });
        };
        assets::AssetPlugin asset_plugin;
        asset_plugin.mode = assets::AssetServerMode::Unprocessed;
        asset_plugin.register_asset_source(assets::AssetSourceId{}, source());
        asset_plugin.register_asset_source(assets::AssetSourceId{std::string{"custom2d"}}, source());
        asset_plugin.register_asset_source(assets::AssetSourceId{std::string{"x"}}, source());
        app.add_events<epix::window::WindowClosed>();
        app.add_plugins(epix::time::TimePlugin{}, epix::camera::CameraPlugin{}, std::move(asset_plugin),
                        epix::image::ImagePlugin{}, mesh::MeshPlugin{}, epix::transform::TransformPlugin{},
                        shader::ShaderPlugin{}, render::FrameCountPlugin{});
        try {
            render::WgpuSettings settings;
            if (uniform_fallback()) {
                auto constrained = settings.limits;
                constrained.maxStorageBuffersPerShaderStage = 0;
                settings.constrained_limits = constrained;
            }
            app.add_plugins(render::RenderPlugin{
                .render_creation = render::RenderCreation::automatic(std::move(settings)),
                .synchronous_pipeline_compilation = true});
        } catch (const std::exception& error) {
            GTEST_SKIP() << "GPU/Vulkan unavailable: " << error.what();
        }
        app.add_plugins(epix::core_graph::CoreGraphPlugin{}, sprite_render::SpriteRenderPlugin{},
                        sprite_render::Material2dPlugin<ConsumerMaterial2d>{});
        app.resource_scope([&](epix::app::internal::Plugins& plugins) { plugins.ready_all(app); });
        auto& render_app = app.get_sub_app_mut(render::Render)->get();
        world            = &render_app.world_mut();
        render_app.run_schedule(render::RenderStartup);
    }

    void TearDown() override { ConsumerMaterial2d::references = {}; }

    template <typename M>
    auto prepare(M material) -> std::optional<sprite_render::PreparedMaterial2d<M>> {
        using Asset       = render::RenderAsset<sprite_render::PreparedMaterial2d<M>>;
        const auto handle = app.resource_mut<assets::Assets<M>>().emplace(material);
        std::optional<sprite_render::PreparedMaterial2d<M>> prepared;
        auto system = ecs::make_system_unique([&](typename Asset::Param params) {
            auto result = Asset{}.prepare_asset(std::move(material), handle.id(), params, nullptr);
            EXPECT_TRUE(result.has_value());
            if (result) prepared = std::move(*result);
        });
        system->initialize(*world);
        EXPECT_TRUE(system->run({}, *world).has_value());
        system->apply_deferred(*world);
        return prepared;
    }

    void initialize_pipeline() {
        auto system = ecs::make_system_unique(sprite_render::init_material_2d_pipeline<ConsumerMaterial2d>);
        system->initialize(*world);
        ASSERT_TRUE(system->run({}, *world));
        system->apply_deferred(*world);
    }

    static mesh::MeshVertexBufferLayoutRef mesh_layout() {
        mesh::MeshVertexBufferLayouts layouts;
        return mesh::Rectangle(1.0f, 1.0f).mesh().build().get_mesh_vertex_buffer_layout(layouts);
    }

    void wait_for(const auto& handles) {
        const auto& server = app.resource<assets::AssetServer>();
        const auto loaded  = [&] {
            return std::ranges::all_of(
                handles, [&](const auto& handle) { return server.is_loaded_with_dependencies(handle.id()); });
        };
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!loaded() && std::chrono::steady_clock::now() < deadline) {
            app.run_schedule(epix::app::PreUpdate);
            app.run_schedule(epix::app::PostUpdate);
            app.run_schedule(epix::app::Last);
            std::this_thread::yield();
        }
        ASSERT_TRUE(loaded());
    }

    std::array<assets::Handle<shader::Shader>, 2> manual_shaders() {
        auto& shaders = app.resource_mut<assets::Assets<shader::Shader>>();
        return {
            shaders.emplace(shader::Shader::from_slang(std::string{shader_text}, assets::AssetPath{"manual/a.slang"})),
            shaders.emplace(shader::Shader::from_slang(std::string{shader_text}, assets::AssetPath{"manual/b.slang"}))};
    }
};

TEST_F(Material2dPipeline, StartupUsesDefaultShadersAndPreparationRetainsAssociatedData) {
    const auto& pipeline = world->resource<sprite_render::Material2dPipeline<ConsumerMaterial2d>>();
    EXPECT_FALSE(pipeline.vertex_shader);
    EXPECT_FALSE(pipeline.fragment_shader);
    const auto prepared = prepare(ConsumerMaterial2d{.variant = 17});
    ASSERT_TRUE(prepared);
    EXPECT_EQ(prepared->key, 17);
    EXPECT_TRUE(prepared->bind_group);
}

TEST_F(Material2dPipeline, InitializationRetainsBothShaderHandles) {
    const auto handles = manual_shaders();
    for (int index = 0; index < 2; ++index)
        ConsumerMaterial2d::references[index] = shader::ShaderRef::from_handle(handles[index]);
    initialize_pipeline();
    const auto& pipeline = world->resource<sprite_render::Material2dPipeline<ConsumerMaterial2d>>();
    ASSERT_TRUE(pipeline.vertex_shader);
    ASSERT_TRUE(pipeline.fragment_shader);
    EXPECT_EQ(*pipeline.vertex_shader, handles[0]);
    EXPECT_EQ(*pipeline.fragment_shader, handles[1]);
}

TEST_F(Material2dPipeline, InitializationPreservesDefaultNamedSingleLetterAndEmbeddedSources) {
    app.resource_mut<assets::EmbeddedAssetRegistry>().insert_asset_static("tests/custom2d/custom.slang",
                                                                          std::as_bytes(std::span{shader_text}));
    const std::array<std::string, 4> paths{"shaders/custom.slang", "custom2d://shaders/custom.slang",
                                           "x://shaders/custom.slang", "embedded://tests/custom2d/custom.slang"};
    std::vector<assets::Handle<shader::Shader>> handles;
    for (const auto& path : paths) handles.push_back(app.resource<assets::AssetServer>().load<shader::Shader>(path));
    for (std::size_t index = 0; index < paths.size(); ++index) {
        ConsumerMaterial2d::references[0] = shader::ShaderRef::from_str(paths[index]);
        ConsumerMaterial2d::references[1] = shader::ShaderRef::from_str(paths[(index + 1) % paths.size()]);
        initialize_pipeline();
        const auto& pipeline = world->resource<sprite_render::Material2dPipeline<ConsumerMaterial2d>>();
        ASSERT_TRUE(pipeline.vertex_shader);
        ASSERT_TRUE(pipeline.fragment_shader);
        EXPECT_EQ(*pipeline.vertex_shader, handles[index]);
        EXPECT_EQ(*pipeline.fragment_shader, handles[(index + 1) % handles.size()]);
    }
    wait_for(handles);
}

TEST_F(Material2dPipeline, PreparedCallbackSeesDataLayoutDefinesAndResolvedShaderOverrides) {
    const auto handles = manual_shaders();
    for (int index = 0; index < 2; ++index)
        ConsumerMaterial2d::references[index] = shader::ShaderRef::from_handle(handles[index]);
    initialize_pipeline();
    const auto prepared = prepare(ConsumerMaterial2d{.variant = 29});
    ASSERT_TRUE(prepared);
    const auto& pipeline = world->resource<sprite_render::Material2dPipeline<ConsumerMaterial2d>>();
    const auto layout    = mesh_layout();
    const sprite_render::Material2dKey<ConsumerMaterial2d> key{.mesh_key = prepared->properties.mesh_pipeline_key_bits,
                                                               .bind_group_data = prepared->key};
    const auto descriptor = pipeline.specialize(key, layout);
    ASSERT_TRUE(descriptor);
    EXPECT_EQ(ConsumerMaterial2d::calls, 1);
    EXPECT_EQ(ConsumerMaterial2d::seen_data, 29);
    EXPECT_EQ(ConsumerMaterial2d::seen_mesh_key, key.mesh_key);
    EXPECT_EQ(ConsumerMaterial2d::seen_layout, layout.value.get());
    EXPECT_TRUE(ConsumerMaterial2d::saw_material_defines);
    EXPECT_TRUE(ConsumerMaterial2d::saw_three_layouts);
    EXPECT_EQ(descriptor->label, "consumer material2d 29");
    EXPECT_EQ(descriptor->vertex.shader, handles[0]);
    ASSERT_TRUE(descriptor->fragment);
    EXPECT_EQ(descriptor->fragment->shader, handles[1]);
    ASSERT_EQ(descriptor->layouts.size(), 3u);
    EXPECT_EQ(descriptor->layouts[0], pipeline.mesh2d_pipeline.view_layout);
    EXPECT_EQ(descriptor->layouts[1], pipeline.mesh2d_pipeline.mesh_layout);
    EXPECT_EQ(descriptor->layouts[2], pipeline.material2d_layout);
}

TEST_F(Material2dPipeline, CallbackErrorsArePropagatedAndFailedSpecializationsAreRetried) {
    const auto prepared = prepare(ConsumerMaterial2d{.variant = -1});
    ASSERT_TRUE(prepared);
    const auto& pipeline = world->resource<sprite_render::Material2dPipeline<ConsumerMaterial2d>>();
    auto& cache =
        world->resource_mut<render::SpecializedMeshPipelines<sprite_render::Material2dPipeline<ConsumerMaterial2d>>>();
    const sprite_render::Material2dKey<ConsumerMaterial2d> key{.mesh_key = prepared->properties.mesh_pipeline_key_bits,
                                                               .bind_group_data = prepared->key};
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto result = cache.specialize(world->resource<render::PipelineServer>(), pipeline, key, mesh_layout());
        ASSERT_FALSE(result);
        const auto& error = std::get<mesh::MissingVertexAttributeError>(result.error());
        EXPECT_EQ(error.id.value, 909u);
        EXPECT_EQ(error.name, "callback requested attribute");
        ASSERT_TRUE(error.pipeline_type);
        EXPECT_EQ(*error.pipeline_type,
                  epix::meta::type_id<sprite_render::Material2dPipeline<ConsumerMaterial2d>>().short_name());
    }
    EXPECT_EQ(ConsumerMaterial2d::calls, 2);
}

TEST_F(Material2dPipeline, AssociatedDataSeparatesPipelinesAndCacheHitsReuseThem) {
    const auto first  = prepare(ConsumerMaterial2d{.variant = 1});
    const auto second = prepare(ConsumerMaterial2d{.variant = 2});
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    const auto& pipeline = world->resource<sprite_render::Material2dPipeline<ConsumerMaterial2d>>();
    auto& cache =
        world->resource_mut<render::SpecializedMeshPipelines<sprite_render::Material2dPipeline<ConsumerMaterial2d>>>();
    const auto& server   = world->resource<render::PipelineServer>();
    const auto layout    = mesh_layout();
    const auto first_key = sprite_render::Material2dKey<ConsumerMaterial2d>{
        .mesh_key = first->properties.mesh_pipeline_key_bits, .bind_group_data = first->key};
    const auto second_key = sprite_render::Material2dKey<ConsumerMaterial2d>{
        .mesh_key = second->properties.mesh_pipeline_key_bits, .bind_group_data = second->key};
    const auto first_id  = cache.specialize(server, pipeline, first_key, layout);
    const auto second_id = cache.specialize(server, pipeline, second_key, layout);
    ASSERT_TRUE(first_id);
    ASSERT_TRUE(second_id);
    EXPECT_NE(*first_id, *second_id);
    EXPECT_EQ(ConsumerMaterial2d::calls, 2);
    const auto reused_id = cache.specialize(server, pipeline, first_key, layout);
    ASSERT_TRUE(reused_id);
    EXPECT_EQ(*reused_id, *first_id);
    EXPECT_EQ(ConsumerMaterial2d::calls, 2);
}

// Read the real prepared colour bind group through a fragment shader. Its
// uniform is vertex/fragment-visible and is intentionally not CopySrc, so this
// checks uploaded bytes without changing production buffer usage or visibility.
std::optional<std::array<float, 4>> read_colour_uniform(
    ecs::World& world, const sprite_render::PreparedMaterial2d<sprite_render::ColorMaterial>& material) {
    const auto& device   = world.resource<wgpu::Device>();
    const auto& queue    = world.resource<wgpu::Queue>();
    const auto& pipeline = world.resource<sprite_render::Material2dPipeline<sprite_render::ColorMaterial>>();
    const auto layout    = world.resource<render::PipelineServer>().get_bind_group_layout(pipeline.material2d_layout);
    constexpr std::string_view wgsl = R"wgsl(
struct Uniforms {
    color: vec4f, uv0: vec4f, uv1: vec4f, uv2: vec4f,
    flags: u32, cutoff: f32, padding: vec2f,
};
@group(0) @binding(0) var<uniform> material: Uniforms;
@vertex fn vertex(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {
    let position = vec2f(f32((index << 1u) & 2u), f32(index & 2u)) * 2.0 - vec2f(1.0);
    return vec4f(position, 0.0, 1.0);
}
@fragment fn fragment() -> @location(0) vec4f {
    return vec4f(material.color.r, material.color.g, material.uv0.x, material.cutoff);
}
)wgsl";
    const auto module               = device.createShaderModule(
        wgpu::ShaderModuleDescriptor().setNextInChain(wgpu::ShaderSourceWGSL().setCode(wgpu::StringView{wgsl})));
    const auto native_layout =
        device.createPipelineLayout(wgpu::PipelineLayoutDescriptor().setBindGroupLayouts(std::array{layout}));
    const auto native_pipeline = device.createRenderPipeline(
        wgpu::RenderPipelineDescriptor()
            .setLayout(native_layout)
            .setVertex(wgpu::VertexState().setModule(module).setEntryPoint("vertex"))
            .setFragment(wgpu::FragmentState()
                             .setModule(module)
                             .setEntryPoint("fragment")
                             .setTargets(std::array{wgpu::ColorTargetState()
                                                        .setFormat(wgpu::TextureFormat::eRGBA32Float)
                                                        .setWriteMask(wgpu::ColorWriteMask::eAll)}))
            .setPrimitive(wgpu::PrimitiveState().setTopology(wgpu::PrimitiveTopology::eTriangleList))
            .setMultisample(wgpu::MultisampleState().setCount(1).setMask(0xffffffffu)));
    if (!module || !native_pipeline) return std::nullopt;
    const auto target =
        device.createTexture(wgpu::TextureDescriptor()
                                 .setSize({1, 1, 1})
                                 .setDimension(wgpu::TextureDimension::e2D)
                                 .setFormat(wgpu::TextureFormat::eRGBA32Float)
                                 .setMipLevelCount(1)
                                 .setSampleCount(1)
                                 .setUsage(wgpu::TextureUsage::eRenderAttachment | wgpu::TextureUsage::eCopySrc));
    const auto staging = device.createBuffer(
        wgpu::BufferDescriptor().setSize(256).setUsage(wgpu::BufferUsage::eCopyDst | wgpu::BufferUsage::eMapRead));
    const auto encoder = device.createCommandEncoder();
    const auto pass    = encoder.beginRenderPass(
        wgpu::RenderPassDescriptor().setColorAttachments(std::array{wgpu::RenderPassColorAttachment()
                                                                        .setView(target.createView())
                                                                        .setLoadOp(wgpu::LoadOp::eClear)
                                                                        .setStoreOp(wgpu::StoreOp::eStore)
                                                                        .setClearValue(wgpu::Color{0, 0, 0, 0})}));
    pass.setPipeline(native_pipeline);
    pass.setBindGroup(0, material.bind_group, std::span<const std::uint32_t>{});
    pass.draw(3, 1, 0, 0);
    pass.end();
    encoder.copyTextureToBuffer(wgpu::TexelCopyTextureInfo().setTexture(target),
                                wgpu::TexelCopyBufferInfo().setBuffer(staging).setLayout(
                                    wgpu::TexelCopyBufferLayout().setBytesPerRow(256).setRowsPerImage(1)),
                                {1, 1, 1});
    queue.submit(encoder.finish());
    struct Result {
        std::atomic_bool completed = false;
        bool mapped                = false;
        std::array<float, 4> value{};
    };
    const auto result = std::make_shared<Result>();
    staging.mapAsync(
        wgpu::MapMode::eRead, 0, 256,
        wgpu::BufferMapCallbackInfo()
            .setMode(wgpu::CallbackMode::eAllowProcessEvents)
            .setCallback(wgpu::BufferMapCallback([result, staging](wgpu::MapAsyncStatus status, wgpu::StringView) {
                if (status == wgpu::MapAsyncStatus::eSuccess) {
                    std::memcpy(result->value.data(), staging.getConstMappedRange(0, 256), sizeof(result->value));
                    staging.unmap();
                    result->mapped = true;
                }
                result->completed.store(true, std::memory_order_release);
            })));
    for (int attempt = 0; attempt < 1000 && !result->completed.load(std::memory_order_acquire); ++attempt)
        device.poll(wgpu::Bool(true));
    if (!result->completed.load(std::memory_order_acquire) || !result->mapped) return std::nullopt;
    return result->value;
}

TEST_F(Material2dPipeline, ColourMaterialsReusePipelinesButUploadDistinctUniforms) {
    auto a = sprite_render::ColorMaterial::from_color(epix::color::Color::linear_rgba(0.25f, 0.5f, 0.75f, 1.0f));
    auto b = sprite_render::ColorMaterial::from_color(epix::color::Color::linear_rgba(0.8f, 0.2f, 0.1f, 1.0f));
    a.mode = sprite_render::AlphaMode2dMask{0.25f};
    b.mode = sprite_render::AlphaMode2dMask{0.75f};
    b.uv_transform[0][0] = 1.5f;
    const auto first     = prepare(a);
    const auto second    = prepare(b);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(first->key, second->key);
    EXPECT_NE(first->bind_group, second->bind_group);
    using Material       = sprite_render::ColorMaterial;
    const auto& pipeline = world->resource<sprite_render::Material2dPipeline<Material>>();
    auto& cache = world->resource_mut<render::SpecializedMeshPipelines<sprite_render::Material2dPipeline<Material>>>();
    const auto& server = world->resource<render::PipelineServer>();
    const auto layout  = mesh_layout();
    const auto first_id =
        cache.specialize(server, pipeline,
                         sprite_render::Material2dKey<Material>{.mesh_key = first->properties.mesh_pipeline_key_bits,
                                                                .bind_group_data = first->key},
                         layout);
    const auto second_id =
        cache.specialize(server, pipeline,
                         sprite_render::Material2dKey<Material>{.mesh_key = second->properties.mesh_pipeline_key_bits,
                                                                .bind_group_data = second->key},
                         layout);
    ASSERT_TRUE(first_id);
    ASSERT_TRUE(second_id);
    EXPECT_EQ(*first_id, *second_id);
    const auto first_uniform  = read_colour_uniform(*world, *first);
    const auto second_uniform = read_colour_uniform(*world, *second);
    ASSERT_TRUE(first_uniform);
    ASSERT_TRUE(second_uniform);
    const std::array<float, 4> expected_first{0.25f, 0.5f, 1.0f, 0.25f};
    const std::array<float, 4> expected_second{0.8f, 0.2f, 1.5f, 0.75f};
    for (std::size_t index = 0; index < 4; ++index) {
        EXPECT_NEAR((*first_uniform)[index], expected_first[index], 1e-6f);
        EXPECT_NEAR((*second_uniform)[index], expected_second[index], 1e-6f);
    }
}

void Material2dPipeline::check_transform_records() {
    constexpr std::string_view source = R"slang(
import epix.mesh2d_functions;
import epix.mesh2d_bindings;
using namespace epix::mesh2d_functions;
struct Output {
    float4 position : SV_Position;
    nointerpolation [[vk::location(0)]] float4 value0;
    nointerpolation [[vk::location(1)]] float4 value1;
    nointerpolation [[vk::location(2)]] float4 value2;
    nointerpolation [[vk::location(3)]] float4 value3;
    nointerpolation [[vk::location(4)]] float4 value4;
    nointerpolation [[vk::location(5)]] float4 value5;
};
[shader("vertex")]
Output vertex(uint index : SV_VulkanVertexID, uint instance : SV_VulkanInstanceID) {
    Output output = {};
    output.position = float4(float2(float((index << 1u) & 2u), float(index & 2u)) * 2.0 - float2(1.0), 0.5, 1.0);
    float4x4 world = get_world_from_local(instance);
    float4 position = float4(0.25, -0.5, 1.2, 1.0);
    output.value0 = mesh2d_position_local_to_world(world, position);
    output.value1 = mesh2d_position_world_to_clip(output.value0);
    output.value2 = mesh2d_position_local_to_clip(world, position);
    output.value3 = float4(mesh2d_normal_local_to_world(float3(0.3, 0.7, -0.2), instance), 0.0);
    output.value4 = mesh2d_tangent_local_to_world(world, float4(0.8, -0.1, 0.3, -1.0));
    output.value5 = float4(float(get_tag(instance)), float(epix::mesh2d_bindings::get_mesh(instance).flags), 0.0, 0.0);
    return output;
}
[shader("fragment")]
float4 fragment(Output input) : SV_Target0 {
    switch (uint(input.position.x)) {
        case 0: return input.value0;
        case 1: return input.value1;
        case 2: return input.value2;
        case 3: return input.value3;
        case 4: return input.value4;
        default: return input.value5;
    }
}
)slang";
    app.resource_mut<assets::EmbeddedAssetRegistry>().insert_asset_static("tests/custom2d/transform_oracle.slang",
                                                                          std::as_bytes(std::span{source}));
    const auto& asset_server = app.resource<assets::AssetServer>();
    const auto oracle        = asset_server.load<shader::Shader>("embedded://tests/custom2d/transform_oracle.slang");
    const auto maths         = asset_server.load<shader::Shader>("embedded://epix/shaders/maths.slang");
    wait_for(std::array{oracle, maths});
    const auto& device = world->resource<wgpu::Device>();
    const auto& queue  = world->resource<wgpu::Queue>();
    const auto& server = app.resource<render::PipelineServer>();
    render::view::ViewUniform view;
    view.clip_from_world = glm::perspectiveRH_ZO(0.9f, 1.6f, 0.1f, 100.0f);
    wgpu::Limits limits{};
    ASSERT_EQ(device.getLimits(&limits), wgpu::Status::eSuccess);
    if (uniform_fallback()) ASSERT_EQ(limits.maxStorageBuffersPerShaderStage, 0u);
    const auto batch = render::render_resource::GpuArrayBuffer<sprite_render::Mesh2dUniform>::batch_size(limits);
    const auto bindings =
        asset_server.get_handle<shader::Shader>(assets::AssetPath{"embedded://sprite/mesh2d_bindings.slang"});
    ASSERT_TRUE(bindings);
    wait_for(std::array{*bindings});
    const auto library = app.resource<assets::Assets<shader::Shader>>().get(bindings->id());
    ASSERT_TRUE(library);
    std::vector<shader::ShaderDefVal> expected_defs;
    if (batch) expected_defs.push_back(shader::ShaderDefVal::from_uint("PER_OBJECT_BUFFER_BATCH_SIZE", *batch));
    EXPECT_EQ(library->get().shader_defs, expected_defs)
        << "batch size belongs to the imported library, not a test override";
    const auto validation_ok = [&] {
        struct Validation {
            bool done  = false;
            bool valid = false;
            std::string message;
        };
        const auto result = std::make_shared<Validation>();
        device.popErrorScope(
            wgpu::PopErrorScopeCallbackInfo()
                .setMode(wgpu::CallbackMode::eAllowProcessEvents)
                .setCallback(wgpu::PopErrorScopeCallback([result](wgpu::PopErrorScopeStatus status,
                                                                  wgpu::ErrorType type, wgpu::StringView message) {
                    result->done  = true;
                    result->valid = status == wgpu::PopErrorScopeStatus::eSuccess && type == wgpu::ErrorType::eNoError;
                    if (!result->valid) result->message = std::string(std::string_view(message));
                })));
        for (unsigned attempt = 0; attempt < 1000 && !result->done; ++attempt) device.poll(wgpu::Bool(true));
        EXPECT_TRUE(result->done);
        EXPECT_TRUE(result->valid) << result->message;
        return result->valid;
    };
    device.pushErrorScope(wgpu::ErrorFilter::eValidation);
    auto probe   = world->resource<sprite_render::Mesh2dPipeline>();
    probe.shader = oracle;
    mesh::MeshVertexBufferLayouts layouts;
    const auto empty_mesh = mesh::Mesh{wgpu::PrimitiveTopology::eTriangleList, assets::RenderAssetUsages::MAIN_WORLD}
                                .get_mesh_vertex_buffer_layout(layouts);
    auto descriptor       = probe.specialize(
        sprite_render::Mesh2dPipelineKey::from_primitive_topology(wgpu::PrimitiveTopology::eTriangleList), empty_mesh);
    ASSERT_TRUE(descriptor);
    ASSERT_TRUE(descriptor->fragment);
    // Float colour output is a measurement surface, not a replacement material:
    // keep production descriptor selection, layouts and imported shader settings.
    descriptor->fragment->targets[0].setFormat(wgpu::TextureFormat::eRGBA32Float);
    descriptor->depth_stencil.reset();
    EXPECT_FALSE(std::ranges::any_of(descriptor->vertex.shader_defs,
                                     [](const auto& def) { return def.name == "PER_OBJECT_BUFFER_BATCH_SIZE"; }));
    const auto id = server.queue_render_pipeline(*descriptor);
    for (unsigned attempt = 0; attempt < 500 && !server.get_render_pipeline(id); ++attempt) {
        app.run_schedule(epix::app::PreUpdate);
        app.run_schedule(epix::app::PostUpdate);
        app.run_schedule(epix::app::Last);
        std::this_thread::yield();
    }
    ASSERT_TRUE(validation_ok());
    const auto pipeline = server.get_render_pipeline(id);
    ASSERT_TRUE(pipeline);
    const auto run = [&](auto function) {
        auto system = ecs::make_system_unique(function);
        system->initialize(*world);
        ASSERT_TRUE(system->run({}, *world));
        system->apply_deferred(*world);
    };
    device.pushErrorScope(wgpu::ErrorFilter::eValidation);
    auto& views = world->resource_mut<render::view::ViewUniforms>().uniforms;
    views.clear();
    views.push(render::view::ViewUniform{});
    const auto view_offset = static_cast<std::uint32_t>(views.push(view));
    views.write_buffer(device, queue);
    auto& globals     = world->resource_mut<render::GlobalsBuffer>().buffer;
    globals.get_mut() = render::GlobalsUniform{};
    globals.write_buffer(device, queue);
    // This headless fixture has no ExtractSchedule; provide the real LUT
    // resource that ExtractResourcePlugin copies from the main world.
    world->insert_resource(app.resource<epix::core_graph::TonemappingLuts>());
    const auto view_entity =
        world
            ->spawn(render::view::ExtractedView{.retained_view_entity =
                                                    render::view::RetainedViewEntity{
                                                        render::sync_world::MainEntity{ecs::Entity::from_index(5000)}},
                                                .clip_from_view  = view.clip_from_world,
                                                .world_from_view = epix::transform::GlobalTransform{glm::mat4(1.0f)}},
                    epix::camera::Camera2d{}, epix::core_graph::Tonemapping::None)
            .id();
    run(sprite_render::prepare_mesh2d_view_bind_groups);
    const auto view_group = world->entity(view_entity).get<sprite_render::Mesh2dViewBindGroup>();
    ASSERT_TRUE(view_group);
    using Values = std::array<glm::vec4, 6>;
    const auto output =
        device.createTexture(wgpu::TextureDescriptor()
                                 .setSize({6, 1, 1})
                                 .setDimension(wgpu::TextureDimension::e2D)
                                 .setFormat(wgpu::TextureFormat::eRGBA32Float)
                                 .setMipLevelCount(1)
                                 .setSampleCount(1)
                                 .setUsage(wgpu::TextureUsage::eRenderAttachment | wgpu::TextureUsage::eCopySrc));
    const auto output_view = output.createView();
    const auto staging     = device.createBuffer(
        wgpu::BufferDescriptor().setSize(256).setUsage(wgpu::BufferUsage::eCopyDst | wgpu::BufferUsage::eMapRead));
    ASSERT_TRUE(validation_ok());
    for (const bool mirrored : {false, true}) {
        SCOPED_TRACE(mirrored ? "mirrored" : "positive determinant");
        const auto transform = glm::translate(glm::mat4{1}, glm::vec3{3, -2, 5}) *
                               glm::rotate(glm::mat4{1}, 0.45f, glm::normalize(glm::vec3{0.2f, 0.7f, 0.4f})) *
                               glm::scale(glm::mat4{1}, glm::vec3{mirrored ? -1.7f : 1.7f, 0.6f, 2.3f});
        device.pushErrorScope(wgpu::ErrorFilter::eValidation);
        auto& instances = world->resource_mut<render::batching::BatchedInstanceBuffer<sprite_render::Mesh2dUniform>>();
        instances.buffer.clear();
        const auto sentinel = sprite_render::Mesh2dUniform::from_components(
            {.world_from_local = glm::translate(glm::mat4{1}, glm::vec3{-50, 100, -10}), .flags = 1}, 5);
        for (unsigned i = 0; i <= batch.value_or(1u); ++i) instances.buffer.push(sentinel);
        const auto index = instances.buffer.push(
            sprite_render::Mesh2dUniform::from_components({.world_from_local = transform, .flags = 66}, 743));
        instances.buffer.write_buffer(device, queue);
        if (batch) {
            ASSERT_TRUE(index.dynamic_offset);
            EXPECT_GT(*index.dynamic_offset, 0u);
            EXPECT_EQ(index.index, 1u);
            const auto binding = instances.instance_data_binding();
            ASSERT_TRUE(binding);
            EXPECT_GT(binding->buffer.getSize(), limits.maxUniformBufferBindingSize);
        } else
            EXPECT_EQ(index.index, 2u);
        run(sprite_render::prepare_mesh2d_bind_group);
        ASSERT_TRUE(world->get_resource<sprite_render::Mesh2dBindGroup>());
        const auto encoder = device.createCommandEncoder();
        const auto pass    = encoder.beginRenderPass(
            wgpu::RenderPassDescriptor().setColorAttachments(std::array{wgpu::RenderPassColorAttachment()
                                                                            .setView(output_view)
                                                                            .setLoadOp(wgpu::LoadOp::eClear)
                                                                            .setStoreOp(wgpu::StoreOp::eStore)
                                                                            .setClearValue(wgpu::Color(0, 0, 0, 0))}));
        pass.setPipeline(pipeline->get().pipeline());
        pass.setBindGroup(0, view_group->get().value, std::array<std::uint32_t, 1>{view_offset});
        const auto& model_group = world->resource<sprite_render::Mesh2dBindGroup>().value;
        if (index.dynamic_offset)
            pass.setBindGroup(1, model_group, std::array<std::uint32_t, 1>{*index.dynamic_offset});
        else
            pass.setBindGroup(1, model_group, std::span<const std::uint32_t>{});
        pass.draw(3, 1, 0, index.index);
        pass.end();
        encoder.copyTextureToBuffer(wgpu::TexelCopyTextureInfo().setTexture(output),
                                    wgpu::TexelCopyBufferInfo().setBuffer(staging).setLayout(
                                        wgpu::TexelCopyBufferLayout().setBytesPerRow(256).setRowsPerImage(1)),
                                    {6, 1, 1});
        const auto commands = encoder.finish();
        ASSERT_TRUE(validation_ok());
        device.pushErrorScope(wgpu::ErrorFilter::eValidation);
        queue.submit(commands);
        ASSERT_TRUE(validation_ok());
        struct Result {
            std::atomic_bool completed = false;
            bool mapped                = false;
            Values values{};
        };
        const auto result = std::make_shared<Result>();
        staging.mapAsync(
            wgpu::MapMode::eRead, 0, 256,
            wgpu::BufferMapCallbackInfo()
                .setMode(wgpu::CallbackMode::eAllowProcessEvents)
                .setCallback(wgpu::BufferMapCallback([result, staging](wgpu::MapAsyncStatus status, wgpu::StringView) {
                    if (status == wgpu::MapAsyncStatus::eSuccess) {
                        std::memcpy(result->values.data(), staging.getConstMappedRange(0, 256), sizeof(Values));
                        staging.unmap();
                        result->mapped = true;
                    }
                    result->completed.store(true, std::memory_order_release);
                })));
        for (int attempt = 0; attempt < 1000 && !result->completed.load(std::memory_order_acquire); ++attempt)
            device.poll(wgpu::Bool(true));
        ASSERT_TRUE(result->completed.load(std::memory_order_acquire));
        ASSERT_TRUE(result->mapped);
        const glm::vec4 position = transform * glm::vec4{0.25f, -0.5f, 1.2f, 1.0f};
        const auto normal_matrix = glm::transpose(glm::inverse(glm::mat3{transform}));
        const Values expected{position,
                              view.clip_from_world * position,
                              view.clip_from_world * position,
                              glm::vec4{normal_matrix * glm::vec3{0.3f, 0.7f, -0.2f}, 0},
                              glm::vec4{glm::mat3{transform} * glm::vec3{0.8f, -0.1f, 0.3f}, -1},
                              glm::vec4{743, 66, 0, 0}};
        for (std::size_t index = 0; index < expected.size(); ++index) {
            SCOPED_TRACE(index);
            for (int component = 0; component < 4; ++component)
                EXPECT_NEAR(result->values[index][component], expected[index][component], 2e-4f);
        }
    }
}

TEST_F(Material2dPipeline, SharedTransformLibrariesUseProductionStorageBindings) { check_transform_records(); }

class UniformMaterial2dPipeline : public Material2dPipeline {
   protected:
    bool uniform_fallback() const override { return true; }
};

TEST_F(UniformMaterial2dPipeline, ActualUniformDeviceRendersTheSecondModelBatch) { check_transform_records(); }
}  // namespace
