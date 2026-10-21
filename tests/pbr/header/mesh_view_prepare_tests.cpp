#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph/prepass.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/pbr/mesh_pipeline_resource.hpp>
#include <epix/pbr/mesh_shaders.hpp>
#include <epix/pbr/mesh_uniform.hpp>
#include <epix/pbr/skin.hpp>
#include <epix/pbr/shadow_phase.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/window.hpp>
#include <exception>
#include <cmath>
#include <memory>
#include <numbers>
#include <thread>
#include <variant>

namespace {
namespace ecs    = epix::ecs;
namespace pbr    = epix::pbr;
namespace render = epix::render;
namespace core   = epix::core_graph;

struct IoTaskPoolInit {
    IoTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPool{epix::task::TaskPoolBuilder{}.num_threads(4)});
    }
} io_task_pool_init;

class MeshViewPrepare : public testing::Test {
   protected:
    epix::app::App app = epix::app::App::create();
    ecs::World* world  = nullptr;
    std::uint32_t next_light_main_index = 10000;
    decltype(ecs::make_system_unique(pbr::prepare_lights)) light_preparation;

    void SetUp() override {
        app.add_events<epix::window::WindowClosed>();
        app.add_plugins(epix::time::TimePlugin{})
            .add_plugins(epix::camera::CameraPlugin{})
            .add_plugins(epix::assets::AssetPlugin{})
            .add_plugins(epix::image::ImagePlugin{})
            .add_plugins(epix::shader::ShaderPlugin{});
        try {
            app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
        } catch (const std::exception& error) {
            GTEST_SKIP() << "GPU/Vulkan unavailable: " << error.what();
        }
        world = &app.get_sub_app_mut(render::Render)->get().world_mut();
        world->insert_resource(pbr::GlobalClusterableObjectMeta{wgpu::BufferBindingType::eStorage});
        resource<pbr::LightMeta>();
        resource<pbr::GlobalAmbientLight>();
        resource<pbr::PointLightShadowMap>().size = 16;
        resource<pbr::DirectionalLightShadowMap>().size = 32;
        resource<render::batching::GpuPreprocessingSupport>();
        resource<render::phase::ViewBinnedRenderPhases<pbr::Shadow>>();
    }

    template <class F>
    void run(F function) {
        auto system = ecs::make_system_unique(function);
        system->initialize(*world);
        ASSERT_TRUE(system->run({}, *world).has_value());
        system->apply_deferred(*world);
    }

    void prepare_lights() {
        // Packing/allocation tests still use the real native preparation flow.
        // A raw eligible camera is required before its initial writer can exist.
        if (world->resource<render::camera::SortedCameras>().cameras.empty())
            spawn_shadow_view(next_light_main_index++);
        if (!light_preparation) {
            light_preparation = ecs::make_system_unique(pbr::prepare_lights);
            light_preparation->initialize(*world);
        }
        ASSERT_TRUE(light_preparation->run({}, *world).has_value());
        light_preparation->apply_deferred(*world);
    }

    template <class T>
    void write_uniform(T& buffer, const auto& value) {
        buffer.push(value);
        buffer.write_buffer(world->resource<wgpu::Device>(), world->resource<wgpu::Queue>());
    }

    template <class T>
    T& resource() {
        world->init_resource<T>();
        return world->resource_mut<T>();
    }

    template <class L>
    ecs::Entity spawn_extracted_light(L light) {
        const auto entity = world->spawn(std::move(light),
            render::sync_world::MainEntity{ecs::Entity::from_index(next_light_main_index++)}).id();
        if constexpr (std::same_as<L, pbr::ExtractedPointLight>) {
            if (world->entity(entity).get<L>()->get().spot_light_angles)
                world->entity_mut(entity).insert(epix::camera::Frustum{});
            else world->entity_mut(entity).insert(epix::camera::CubemapFrusta{});
        }
        return entity;
    }

    wgpu::Texture texture(wgpu::TextureFormat format, std::uint32_t layers = 1, std::uint32_t samples = 1) {
        return world->resource<wgpu::Device>().createTexture(
            wgpu::TextureDescriptor()
                .setDimension(wgpu::TextureDimension::e2D)
                .setSize({4, 4, layers})
                .setSampleCount(samples)
                .setFormat(format)
                .setUsage(wgpu::TextureUsage::eTextureBinding | wgpu::TextureUsage::eRenderAttachment));
    }

    void prepare_view_resources(bool binding_arrays = false) {
        prepare_lights();
        const auto& device = world->resource<wgpu::Device>();
        const auto& queue  = world->resource<wgpu::Queue>();
        world->insert_resource(pbr::MeshPipeline{
            .view_layouts = pbr::MeshPipelineViewLayouts::make(wgpu::BufferBindingType::eReadOnlyStorage,
                                                               wgpu::BufferBindingType::eReadOnlyStorage,
                                                               binding_arrays, false, false, false),
            .shader =
                epix::assets::Handle<epix::shader::Shader>{epix::assets::AssetId<epix::shader::Shader>::invalid()},
            .binding_arrays_are_usable = binding_arrays,
        });
        auto& uniforms = resource<render::view::ViewUniforms>();
        write_uniform(uniforms.uniforms, render::view::ViewUniform{});
        auto& lights = resource<pbr::LightMeta>();
        write_uniform(lights.view_gpu_lights, pbr::GpuLights{});
        auto& fog = resource<pbr::FogMeta>();
        write_uniform(fog.gpu_fogs, pbr::GpuFog{});
        auto& probes = resource<pbr::LightProbesBuffer>();
        write_uniform(probes.uniforms, pbr::LightProbesUniform{});
        auto& ssr = resource<pbr::ScreenSpaceReflectionsBuffer>();
        write_uniform(ssr.uniforms, pbr::ScreenSpaceReflectionsUniform{});
        auto& environment = resource<pbr::EnvironmentMapUniformBuffer>();
        write_uniform(environment.uniforms, pbr::EnvironmentMapUniform{});
        auto& globals = resource<render::GlobalsBuffer>();
        globals.buffer.write_buffer(device, queue);
        auto& visibility = resource<render::view::RenderVisibilityRanges>();
        visibility.buffer.push(glm::vec4{0.0f});
        visibility.buffer.write_buffer(device, queue);
        const auto invalid_image =
            epix::assets::Handle<epix::image::Image>{epix::assets::AssetId<epix::image::Image>::invalid()};
        world->insert_resource(core::TonemappingLuts{invalid_image, invalid_image, invalid_image});
        world->insert_resource(pbr::make_shadow_samplers(device));
    }

    ecs::Entity spawn_view(render::view::Msaa msaa) {
        const auto& device            = world->resource<wgpu::Device>();
        const auto& queue             = world->resource<wgpu::Queue>();
        const auto point_shadow       = texture(wgpu::TextureFormat::eDepth32Float, 6);
        const auto directional_shadow = texture(wgpu::TextureFormat::eDepth32Float);
        pbr::ViewShadowBindings shadows{
            .point_light_depth_texture      = point_shadow,
            .point_light_depth_texture_view = point_shadow.createView(
                wgpu::TextureViewDescriptor().setDimension(wgpu::TextureViewDimension::eCubeArray)),
            .directional_light_depth_texture      = directional_shadow,
            .directional_light_depth_texture_view = directional_shadow.createView(
                wgpu::TextureViewDescriptor().setDimension(wgpu::TextureViewDimension::e2DArray)),
        };
        pbr::ViewClusterBindings clusters{wgpu::BufferBindingType::eStorage};
        clusters.push_offset_and_counts(0, {});
        clusters.write_buffers(device, queue);
        return world->spawn(std::move(shadows), std::move(clusters), core::Tonemapping::None, msaa).id();
    }

    static render::view::ExtractedView extracted_view(std::uint32_t index) {
        return {
            .retained_view_entity =
                render::view::RetainedViewEntity{render::sync_world::MainEntity{ecs::Entity::from_index(index)}},
            .clip_from_view = glm::mat4{1.0f},
            .world_from_view = {},
            .viewport = {0, 0, 4, 4},
        };
    }

    ecs::Entity spawn_shadow_view(std::uint32_t index,
                                  std::optional<epix::camera::RenderLayers> layers = std::nullopt) {
        const auto entity = world->spawn(extracted_view(index), epix::camera::Camera3d{},
                                          pbr::ExtractedClusterConfig{},
                                          render::sync_world::MainEntity{ecs::Entity::from_index(index)}).id();
        if (layers) world->entity_mut(entity).insert(*layers);
        resource<render::camera::SortedCameras>().cameras.push_back({.entity = entity});
        return entity;
    }

    void expect_groups(ecs::Entity entity) {
        const auto groups = world->entity(entity).get<pbr::MeshViewBindGroup>();
        ASSERT_TRUE(groups.has_value());
        EXPECT_TRUE(groups->get().main);
        EXPECT_TRUE(groups->get().binding_array);
        EXPECT_TRUE(groups->get().empty);
    }

    template <class T>
    T read_gpu_value(const wgpu::Buffer& source, std::uint32_t offset = 0) {
        const auto& device = world->resource<wgpu::Device>();
        const auto staging = device.createBuffer(wgpu::BufferDescriptor().setSize(sizeof(T)).setUsage(
            wgpu::BufferUsage::eCopyDst | wgpu::BufferUsage::eMapRead));
        const auto encoder = device.createCommandEncoder();
        encoder.copyBufferToBuffer(source, offset, staging, 0, sizeof(T));
        world->resource<wgpu::Queue>().submit(encoder.finish());
        struct Result {
            T value{};
            bool completed = false;
            bool mapped    = false;
        };
        const auto result = std::make_shared<Result>();
        staging.mapAsync(
            wgpu::MapMode::eRead, 0, sizeof(T),
            wgpu::BufferMapCallbackInfo()
                .setMode(wgpu::CallbackMode::eAllowProcessEvents)
                .setCallback(wgpu::BufferMapCallback([result, staging](wgpu::MapAsyncStatus status, wgpu::StringView) {
                    result->completed = true;
                    if (status != wgpu::MapAsyncStatus::eSuccess) return;
                    std::memcpy(&result->value, staging.getConstMappedRange(0, sizeof(T)), sizeof(T));
                    staging.unmap();
                    result->mapped = true;
                })));
        for (int attempt = 0; attempt < 1000 && !result->completed; ++attempt) device.poll(wgpu::Bool(true));
        EXPECT_TRUE(result->mapped);
        return result->value;
    }
};

TEST_F(MeshViewPrepare, EmptyClusterBuffersStillSatisfyTheLayoutMinimum) {
    prepare_lights();
    const auto& meta = world->resource<pbr::GlobalClusterableObjectMeta>();
    ASSERT_TRUE(meta.gpu_clusterable_objects.binding());
    EXPECT_GE(meta.gpu_clusterable_objects.binding()->get().getSize(), sizeof(pbr::GpuClusterableObject));
    EXPECT_TRUE(meta.entity_to_index.empty());
}

TEST_F(MeshViewPrepare, ClusterUploadSortsPointAndSpotLightsAndRebuildsIndices) {
    pbr::ExtractedPointLight spot;
    spot.intensity         = 3.0f;
    spot.range             = 4.0f;
    spot.radius            = 0.5f;
    spot.shadow_map_near_z = 0.1f;
    spot.spot_light_angles = std::pair{0.2f, 0.4f};
    spot.transform[3]      = glm::vec4{1, 2, 3, 1};
    const auto spot_entity = spawn_extracted_light(spot);
    auto point             = spot;
    point.spot_light_angles.reset();
    point.color             = {0.25f, 0.5f, 1.0f, 1.0f};
    const auto point_entity = spawn_extracted_light(point);
    prepare_lights();
    const auto& meta = world->resource<pbr::GlobalClusterableObjectMeta>();
    ASSERT_EQ(meta.entity_to_index.size(), 2u);
    EXPECT_EQ(meta.entity_to_index.at(point_entity), 0u);
    EXPECT_EQ(meta.entity_to_index.at(spot_entity), 1u);
    const auto& data = std::get<pbr::GpuClusterableObjects::Storage>(meta.gpu_clusterable_objects.buffer).get().data;
    ASSERT_EQ(data.size(), 2u);
    EXPECT_EQ(data[0].color_inverse_square_range, (glm::vec4{0.75f, 1.5f, 3.0f, 1.0f / 16.0f}));
    EXPECT_EQ(data[0].position_radius, (glm::vec4{1, 2, 3, 0.5f}));
    EXPECT_EQ(data[0].light_custom_data, (glm::vec4{0, -1, 0.1f, 0}));
    EXPECT_NEAR(data[1].spot_light_tan_angle, std::tan(0.4f), 1e-6f);
    EXPECT_EQ(data[1].decal_index, 0xFFFFFFFFu);
    world->entity_mut(point_entity).remove<pbr::ExtractedPointLight>();
    prepare_lights();
    EXPECT_EQ(meta.entity_to_index.size(), 1u);
    EXPECT_FALSE(meta.entity_to_index.contains(point_entity));
    EXPECT_EQ(meta.entity_to_index.at(spot_entity), 0u);
}

TEST_F(MeshViewPrepare, MainWorldPointAndSpotExtractionReachesUploadedGpuRecords) {
    auto& main = app.world_mut();
    world->insert_resource(epix::app::ExtractedWorld{main});
    main.insert_resource(pbr::PointLightShadowMap{.size = 32});
    main.insert_resource(pbr::DirectionalLightShadowMap{.size = 64});
    const auto point_render = world->spawn().id();
    const auto spot_render = world->spawn().id();
    epix::camera::ViewVisibility visible;
    visible.set_visible();
    glm::mat4 point_transform{1.0f}, spot_transform{1.0f};
    point_transform[3] = {-2, 1, 3, 1};
    spot_transform[3] = {2, 2, 4, 1};
    const auto point = main.spawn(
        pbr::PointLight{.intensity = 80.0f * std::numbers::pi_v<float>, .range = 2.0f,
            .radius = 0.25f, .shadow_normal_bias = 0.7f},
        render::sync_world::RenderEntity{point_render}, epix::transform::GlobalTransform{point_transform},
        visible, epix::camera::CubemapFrusta{}, epix::camera::CubemapVisibleEntities{}).id();
    const auto spot = main.spawn(
        pbr::SpotLight{.intensity = 120.0f * std::numbers::pi_v<float>, .range = 4.0f,
            .radius = 0.5f, .shadow_normal_bias = 1.3f, .outer_angle = 0.6f, .inner_angle = 0.2f},
        render::sync_world::RenderEntity{spot_render}, epix::transform::GlobalTransform{spot_transform},
        visible, epix::camera::Frustum{}, epix::camera::VisibleMeshEntities{}, pbr::VolumetricLight{}).id();
    main.insert_resource(pbr::GlobalVisibleClusterableObjects{.entities = {point, spot}});
    run(pbr::extract_lights);
    auto& meta = world->resource_mut<pbr::GlobalClusterableObjectMeta>();
    auto& storage = std::get<pbr::GpuClusterableObjects::Storage>(meta.gpu_clusterable_objects.buffer);
    storage.add_usages(wgpu::BufferUsage::eCopySrc);
    prepare_lights();
    ASSERT_EQ(meta.entity_to_index.size(), 2u);
    ASSERT_EQ(meta.entity_to_index.at(point_render), 0u);
    ASSERT_EQ(meta.entity_to_index.at(spot_render), 1u);
    ASSERT_TRUE(meta.gpu_clusterable_objects.binding());
    const auto& buffer = meta.gpu_clusterable_objects.binding()->get();
    const auto point_gpu = read_gpu_value<pbr::GpuClusterableObject>(buffer);
    const auto spot_gpu = read_gpu_value<pbr::GpuClusterableObject>(buffer, sizeof(pbr::GpuClusterableObject));
    EXPECT_EQ(point_gpu.color_inverse_square_range, (glm::vec4{20, 20, 20, 0.25f}));
    EXPECT_EQ(spot_gpu.color_inverse_square_range, (glm::vec4{30, 30, 30, 0.0625f}));
    EXPECT_EQ(point_gpu.position_radius, (glm::vec4{-2, 1, 3, 0.25f}));
    EXPECT_EQ(spot_gpu.position_radius, (glm::vec4{2, 2, 4, 0.5f}));
    EXPECT_FLOAT_EQ(point_gpu.shadow_normal_bias, 0.7f * (2.0f / 32.0f) * std::numbers::sqrt2_v<float>);
    EXPECT_FLOAT_EQ(spot_gpu.shadow_normal_bias,
        1.3f * (2.0f * std::tan(0.6f) / 64.0f) * std::numbers::sqrt2_v<float>);
    EXPECT_NEAR(spot_gpu.spot_light_tan_angle, std::tan(0.6f), 1e-6f);
    EXPECT_EQ(point_gpu.flags & pbr::PointLightFlags::VOLUMETRIC, 0u);
    // Bevy preserves marker presence at extraction, but the GPU volumetric
    // flag additionally requires shadows. Check both states through real upload.
    EXPECT_TRUE(world->entity(spot_render).get<pbr::ExtractedPointLight>()->get().volumetric);
    EXPECT_EQ(spot_gpu.flags & pbr::PointLightFlags::VOLUMETRIC, 0u);
    main.entity_mut(spot).get_mut<pbr::SpotLight>()->get_mut().shadows_enabled = true;
    run(pbr::extract_lights);
    prepare_lights();
    const auto shadowed_spot = read_gpu_value<pbr::GpuClusterableObject>(
        meta.gpu_clusterable_objects.binding()->get(), sizeof(pbr::GpuClusterableObject));
    EXPECT_NE(shadowed_spot.flags & pbr::PointLightFlags::VOLUMETRIC, 0u);
    EXPECT_NE(shadowed_spot.flags & pbr::PointLightFlags::SHADOWS_ENABLED, 0u);
}

TEST_F(MeshViewPrepare, BuildsAllThreeGroupsForViewsWithoutProbes) {
    prepare_view_resources();
    const auto view = spawn_view(render::view::Msaa::Off);
    run(pbr::prepare_mesh_view_bind_groups);
    expect_groups(view);
}

TEST_F(MeshViewPrepare, ShadowArraysSumMatchingLightsThenTakeTheLargestView) {
    resource<render::render_resource::TextureCache>();
    world->insert_resource(pbr::PointLightShadowMap{.size = 4});
    world->insert_resource(pbr::DirectionalLightShadowMap{.size = 8});
    const auto default_view = spawn_shadow_view(0);
    const auto layer1_view = spawn_shadow_view(1, epix::camera::RenderLayers::layer(1));
    const auto add_light = [&](std::size_t cascades, std::size_t layer, bool shadows = true) {
        pbr::ExtractedDirectionalLight light;
        light.shadows_enabled = shadows;
        light.render_layers = epix::camera::RenderLayers::layer(layer);
        light.cascade_shadow_config.bounds.resize(cascades);
        // Allocation uses configuration, not the not-yet-built per-view cascade maps.
        return spawn_extracted_light(std::move(light));
    };
    const auto first = add_light(2, 0);
    const auto second = add_light(3, 0);
    add_light(9, 1);  // Capped to four cascades for this light.
    add_light(4, 2);  // No view sees layer 2.
    add_light(4, 0, false);
    pbr::ExtractedPointLight spot;
    spot.shadows_enabled = true;
    spot.spot_light_angles = std::pair{0.2f, 0.4f};
    const auto spot_entity = spawn_extracted_light(spot);

    const auto expect_layers = [&](ecs::Entity view, std::uint32_t layers) {
        const auto bindings = world->entity(view).get<pbr::ViewShadowBindings>();
        ASSERT_TRUE(bindings);
        EXPECT_EQ(bindings->get().directional_light_depth_texture.getDepthOrArrayLayers(), layers);
        EXPECT_EQ(bindings->get().directional_light_depth_texture.getWidth(), 8u);
        EXPECT_EQ(bindings->get().point_light_depth_texture.getDepthOrArrayLayers(), 6u);
        EXPECT_EQ(bindings->get().point_light_depth_texture.getWidth(), 4u);
    };
    prepare_lights();
    expect_layers(default_view, 6);  // max(2 + 3, 4) + one spot.
    expect_layers(layer1_view, 6);
    for (const auto& [entity, count] : std::array{std::pair{first, 2u}, std::pair{second, 3u}}) {
        auto light = world->entity(entity).get<pbr::ExtractedDirectionalLight>()->get();
        light.cascades[default_view].resize(count);
        world->entity_mut(entity).insert(std::move(light));
    }
    prepare_lights();
    expect_layers(default_view, 6);  // Populating cascade maps must not change the allocation.
    world->entity_mut(second).remove<pbr::ExtractedDirectionalLight>();
    prepare_lights();
    expect_layers(default_view, 5);  // max(2, 4) + one spot, not 2 + 4 + 1.
    world->entity_mut(layer1_view).despawn();
    prepare_lights();
    expect_layers(default_view, 3);
    world->entity_mut(first).remove<pbr::ExtractedDirectionalLight>();
    world->entity_mut(spot_entity).remove<pbr::ExtractedPointLight>();
    prepare_lights();
    expect_layers(default_view, 1);  // Unmatched/disabled lights need no layers.
}

TEST_F(MeshViewPrepare, PointShadowArrayKeepsOneCubeAndResizesWithShadowCasters) {
    resource<render::render_resource::TextureCache>();
    world->insert_resource(pbr::PointLightShadowMap{.size = 4});
    world->insert_resource(pbr::DirectionalLightShadowMap{.size = 4});
    const auto view = spawn_shadow_view(0);
    const auto expect_layers = [&](std::uint32_t layers) {
        prepare_lights();
        const auto bindings = world->entity(view).get<pbr::ViewShadowBindings>();
        ASSERT_TRUE(bindings);
        EXPECT_EQ(bindings->get().point_light_depth_texture.getDepthOrArrayLayers(), layers);
        EXPECT_EQ(bindings->get().directional_light_depth_texture.getDepthOrArrayLayers(), 1u);
        for (const auto& [key, textures] : resource<render::render_resource::TextureCache>().textures) {
            for (const auto& cached : textures)
                EXPECT_EQ(key.depth_or_array_layers, cached.texture.getDepthOrArrayLayers());
        }
    };
    expect_layers(6);
    pbr::ExtractedPointLight point;
    point.shadows_enabled = true;
    spawn_extracted_light(point);
    const auto second = spawn_extracted_light(point);
    spawn_extracted_light(pbr::ExtractedPointLight{});
    expect_layers(12);
    world->entity_mut(second).remove<pbr::ExtractedPointLight>();
    expect_layers(6);
}

TEST_F(MeshViewPrepare, ShadowArraysOnlyConsiderSortedClustered3dCameraViews) {
    resource<render::render_resource::TextureCache>();
    world->insert_resource(pbr::PointLightShadowMap{.size = 4});
    world->insert_resource(pbr::DirectionalLightShadowMap{.size = 4});
    const auto view = spawn_shadow_view(0);
    const auto layer1 = epix::camera::RenderLayers::layer(1);
    const auto unsorted = spawn_shadow_view(1, layer1);
    resource<render::camera::SortedCameras>().cameras.pop_back();
    const auto not_3d = spawn_shadow_view(2, layer1);
    world->entity_mut(not_3d).remove<epix::camera::Camera3d>();
    const auto no_clusters = spawn_shadow_view(3, layer1);
    world->entity_mut(no_clusters).remove<pbr::ExtractedClusterConfig>();
    const auto no_main_entity = spawn_shadow_view(4, layer1);
    world->entity_mut(no_main_entity).remove<render::sync_world::MainEntity>();
    resource<render::camera::SortedCameras>().cameras.push_back({.entity = ecs::Entity::from_index(100000)});
    pbr::ExtractedDirectionalLight light;
    light.shadows_enabled = true;
    light.cascade_shadow_config.bounds.resize(2);
    spawn_extracted_light(light);
    light.render_layers = layer1;
    light.cascade_shadow_config.bounds.resize(4);
    spawn_extracted_light(light);
    prepare_lights();
    const auto bindings = world->entity(view).get<pbr::ViewShadowBindings>();
    ASSERT_TRUE(bindings);
    EXPECT_EQ(bindings->get().directional_light_depth_texture.getDepthOrArrayLayers(), 2u);
    for (const auto excluded : {unsorted, not_3d, no_clusters, no_main_entity})
        EXPECT_FALSE(world->entity(excluded).contains<pbr::ViewShadowBindings>());
}

TEST_F(MeshViewPrepare, UsesMultisampledPrepassAndTransmissionTextures) {
    prepare_view_resources();
    const auto view = spawn_view(render::view::Msaa::Sample4);
    core::core_3d::ViewPrepassTextures prepass;
    const auto depth  = texture(wgpu::TextureFormat::eDepth32Float, 1, 4);
    const auto normal = texture(wgpu::TextureFormat::eRGBA16Float, 1, 4);
    const auto motion = texture(wgpu::TextureFormat::eRG16Float, 1, 4);
    prepass.depth     = render::render_resource::ColorAttachment{
        render::render_resource::CachedTexture{depth, depth.createView(wgpu::TextureViewDescriptor{})}, std::nullopt};
    prepass.normal = render::render_resource::ColorAttachment{
        render::render_resource::CachedTexture{normal, normal.createView(wgpu::TextureViewDescriptor{})}, std::nullopt};
    prepass.motion_vectors = render::render_resource::ColorAttachment{
        render::render_resource::CachedTexture{motion, motion.createView(wgpu::TextureViewDescriptor{})}, std::nullopt};
    const auto transmission = texture(wgpu::TextureFormat::eRGBA16Float);
    world->entity_mut(view).insert(std::move(prepass),
                                   core::core_3d::ViewTransmissionTexture{
                                       transmission, transmission.createView(wgpu::TextureViewDescriptor{}),
                                       world->resource<wgpu::Device>().createSampler(wgpu::SamplerDescriptor{})});
    run(pbr::prepare_mesh_view_bind_groups);
    expect_groups(view);
}

TEST_F(MeshViewPrepare, PopulatesProbeBindingArraysOnCapableDevices) {
    if (!world->resource<wgpu::Device>().hasFeature(wgpu::FeatureName::eTextureBindingArray)) {
        GTEST_SKIP() << "Device has no texture binding arrays";
    }
    prepare_view_resources(true);
    const auto view = spawn_view(render::view::Msaa::Off);
    run(pbr::prepare_mesh_view_bind_groups);
    expect_groups(view);
}

TEST_F(MeshViewPrepare, UsesTheUnsignedDeferredPrepassTexture) {
    prepare_view_resources();
    const auto view = spawn_view(render::view::Msaa::Off);
    core::core_3d::ViewPrepassTextures prepass;
    const auto deferred = texture(wgpu::TextureFormat::eRGBA32Uint);
    prepass.deferred    = render::render_resource::ColorAttachment{
        render::render_resource::CachedTexture{deferred, deferred.createView(wgpu::TextureViewDescriptor{})}};
    world->entity_mut(view).insert(std::move(prepass));
    run(pbr::prepare_mesh_view_bind_groups);
    expect_groups(view);
}

TEST_F(MeshViewPrepare, UploadedViewLightsUseBackDirectionAndOrthographicClusters) {
    world->insert_resource(pbr::GlobalAmbientLight{});
    world->insert_resource(pbr::PointLightShadowMap{.size = 16});
    world->insert_resource(pbr::DirectionalLightShadowMap{.size = 32});
    resource<render::batching::GpuPreprocessingSupport>();
    resource<render::phase::ViewBinnedRenderPhases<pbr::Shadow>>();
    auto& lights = resource<pbr::LightMeta>();
    lights.view_gpu_lights.add_usages(wgpu::BufferUsage::eCopySrc);
    const auto view =
        world
            ->spawn(
                render::view::ExtractedView{
                    .retained_view_entity =
                        render::view::RetainedViewEntity{render::sync_world::MainEntity{ecs::Entity::from_index(7)}},
                    .clip_from_view  = glm::mat4{1.0f},
                    .world_from_view = {},
                    .viewport        = {0, 0, 64, 32},
                },
                pbr::ExtractedClusterConfig{.near_plane = 1.0f, .far_plane = 17.0f, .dimensions = {4, 2, 8}},
                epix::camera::Camera3d{}, render::sync_world::MainEntity{ecs::Entity::from_index(7)})
            .id();
    resource<render::camera::SortedCameras>().cameras.push_back({.entity = view});
    pbr::ExtractedDirectionalLight volumetric;
    volumetric.volumetric      = true;
    volumetric.shadows_enabled = true;
    volumetric.illuminance     = 10.0f;
    volumetric.sun_disk_angular_size = 0.0235f;
    volumetric.sun_disk_intensity = 4.25f;
    world->spawn(volumetric, render::sync_world::MainEntity{ecs::Entity::from_index(8)});
    pbr::ExtractedDirectionalLight ordinary;
    ordinary.illuminance  = 2.0f;
    ordinary.transform[2] = {0, 0, 2, 0};
    ordinary.sun_disk_angular_size = pbr::SunDisk::EARTH.angular_size;
    ordinary.sun_disk_intensity = pbr::SunDisk::EARTH.intensity;
    world->spawn(ordinary, render::sync_world::MainEntity{ecs::Entity::from_index(9)});
    prepare_lights();
    const auto offset = world->entity(view).get<pbr::ViewLightsUniformOffset>();
    ASSERT_TRUE(offset);
    const auto uploaded = read_gpu_value<pbr::GpuLights>(*lights.view_gpu_lights.buffer(), offset->get().offset);
    ASSERT_EQ(uploaded.n_directional_lights, 2u);
    EXPECT_EQ(uploaded.directional_lights[0].color, glm::vec4{2.0f});
    EXPECT_EQ(uploaded.directional_lights[0].dir_to_light, (glm::vec3{0, 0, 1}));
    EXPECT_EQ(uploaded.directional_lights[1].color, glm::vec4{10.0f});
    EXPECT_FLOAT_EQ(uploaded.directional_lights[0].sun_disk_angular_size, 0.00930842f);
    EXPECT_FLOAT_EQ(uploaded.directional_lights[0].sun_disk_intensity, 1.0f);
    EXPECT_FLOAT_EQ(uploaded.directional_lights[1].sun_disk_angular_size, 0.0235f);
    EXPECT_FLOAT_EQ(uploaded.directional_lights[1].sun_disk_intensity, 4.25f);
    EXPECT_EQ(uploaded.cluster_factors, (glm::vec4{0.0625f, 0.0625f, -1.0f, -0.5f}));
}

TEST_F(MeshViewPrepare, ViewShaderArithmeticMatchesColumnVectorTransforms) {
    // Exercise the actual imported library, rather than a C++ copy or a shader
    // that merely compiles. Non-symmetric transforms expose reversed operands.
    app.add_plugins(epix::mesh::MeshPlugin{}, epix::transform::TransformPlugin{});
    pbr::register_mesh_shaders(app.world_mut());
    app.add_plugins(render::FrameCountPlugin{});
    static constexpr std::string_view source = R"slang(
import epix.view_transformations;
using namespace epix::view_transformations;
// Leave the imported probe bindings (group 1, bindings 0..7) unaliased.
[[vk::binding(8, 1)]] RWStructuredBuffer<float4> results;
struct ProbeInputs {
    float4 position;
    float4 direction;
    float4 clip;
};
[[vk::binding(9, 1)]] ConstantBuffer<ProbeInputs> inputs;
[shader("compute")]
[numthreads(1, 1, 1)]
void main() {
    float3 p = inputs.position.xyz;
    float3 d = inputs.direction.xyz;
    float4 c = inputs.clip;
    results[0] = float4(position_view_to_world(p), 1);
    results[1] = float4(position_clip_to_world(c), 1);
    results[2] = float4(position_ndc_to_world(c.xyz), 1);
    results[3] = float4(direction_view_to_world(d), 0);
    results[4] = float4(direction_clip_to_world(float4(d, 0)), 0);
    results[5] = float4(position_world_to_view(p), 1);
    results[6] = float4(position_clip_to_view(c), 1);
    results[7] = float4(position_ndc_to_view(c.xyz), 1);
    results[8] = float4(direction_world_to_view(d), 0);
    results[9] = float4(direction_clip_to_view(float4(d, 0)), 0);
    results[10] = float4(position_world_to_prev_view(p), 1);
    results[11] = float4(position_world_to_prev_ndc(p), 1);
    results[12] = position_world_to_clip(p);
    results[13] = position_view_to_clip(p);
    results[14] = direction_world_to_clip(d);
    results[15] = direction_view_to_clip(d);
    results[16] = float4(position_world_to_ndc(p), 1);
    results[17] = float4(position_view_to_ndc(p), 1);
    results[18] = float4(perspective_camera_near(), depth_ndc_to_view_z(0.4),
        view_z_to_depth_ndc(-3.0), prev_view_z_to_depth_ndc(-3.0));
}
)slang";
    app.resource_mut<epix::assets::EmbeddedAssetRegistry>().insert_asset_static(
        "tests/pbr/view_transform_oracle.slang",
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(source.data()), source.size()});
    const auto shader = app.resource<epix::assets::AssetServer>().load<epix::shader::Shader>(
        "embedded://tests/pbr/view_transform_oracle.slang");
    const auto& asset_server = app.resource<epix::assets::AssetServer>();
    const std::array required_shaders{asset_server.load<epix::shader::Shader>("embedded://epix/shaders/view.slang"),
                                      asset_server.load<epix::shader::Shader>("embedded://epix/shaders/globals.slang"),
                                      app.resource<pbr::MeshShaderHandles>().view_transformations, shader};
    const auto dependencies_loaded = [&] {
        return std::ranges::all_of(required_shaders, [&](const auto& handle) {
            return asset_server.is_loaded_with_dependencies(handle.id());
        });
    };
    // Do not queue against a partially populated shader import cache. Previous
    // fixtures leave asynchronous work on the process-wide IO pool too.
    for (int attempt = 0; attempt < 10000 && !dependencies_loaded(); ++attempt) {
        app.run_schedule(epix::app::PreUpdate);
        app.run_schedule(epix::app::PostUpdate);
        app.run_schedule(epix::app::Last);
        std::this_thread::yield();
    }
    ASSERT_TRUE(dependencies_loaded());
    const auto uniform_entry = [](std::uint32_t binding, std::uint64_t size) {
        return wgpu::BindGroupLayoutEntry()
            .setBinding(binding)
            .setVisibility(wgpu::ShaderStage::eCompute)
            .setBuffer(wgpu::BufferBindingLayout().setType(wgpu::BufferBindingType::eUniform).setMinBindingSize(size));
    };
    const auto view_layout = render::BindGroupLayoutDescriptor::create(
        "view oracle uniforms", std::array{uniform_entry(0, sizeof(render::view::ViewUniform)),
                                           uniform_entry(2, sizeof(core::PreviousViewData))});
    const auto output_layout = render::BindGroupLayoutDescriptor::create(
        "view oracle output",
        std::array{wgpu::BindGroupLayoutEntry()
                       .setBinding(8)
                       .setVisibility(wgpu::ShaderStage::eCompute)
                       .setBuffer(wgpu::BufferBindingLayout().setType(wgpu::BufferBindingType::eStorage)),
                   uniform_entry(9, 3 * sizeof(glm::vec4))});
    using Values       = std::array<glm::vec4, 19>;
    const auto& device = world->resource<wgpu::Device>();
    const auto& queue  = world->resource<wgpu::Queue>();
    // Use runtime inputs, as production shaders do. Constant probe vectors expose
    // a Slang constant-folding bug: float4(float3, scalar) becomes an invalid
    // two-constituent OpConstantComposite (VUID-pCode-08737).
    const std::array inputs{glm::vec4{0.25f, -0.5f, -2.0f, 1}, glm::vec4{0.2f, 0.3f, -0.4f, 0},
                            glm::vec4{0.1f, -0.2f, 0.3f, 1}};
    const auto input_buffer =
        device.createBuffer(wgpu::BufferDescriptor()
                                .setSize(sizeof(inputs))
                                .setUsage(wgpu::BufferUsage::eUniform | wgpu::BufferUsage::eCopyDst));
    queue.writeBuffer(input_buffer, 0, inputs.data(), sizeof(inputs));
    const auto output = device.createBuffer(wgpu::BufferDescriptor()
                                                .setSize(sizeof(Values))
                                                .setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopySrc));
    auto& server      = app.resource<render::PipelineServer>();
    render::render_resource::UniformBuffer<render::view::ViewUniform> view_buffer;
    render::render_resource::UniformBuffer<core::PreviousViewData> previous_buffer;

    for (int variant = 0; variant < 3; ++variant) {
        SCOPED_TRACE(variant);  // generic, perspective specialization, orthographic specialization
        render::view::ViewUniform view{};
        view.world_from_view = glm::translate(glm::mat4{1.0f}, glm::vec3{3, -2, 5}) *
                               glm::rotate(glm::mat4{1.0f}, 0.3f, glm::vec3{0, 1, 0});
        view.view_from_world = glm::inverse(view.world_from_view);
        view.clip_from_view  = variant == 2 ? glm::orthoRH_ZO(-3.0f, 5.0f, -2.0f, 4.0f, 0.3f, 17.0f)
                                            : epix::camera::perspective_infinite_reverse_rh(1.1f, 1.5f, 0.3f);
        view.view_from_clip  = glm::inverse(view.clip_from_view);
        view.clip_from_world = view.clip_from_view * view.view_from_world;
        view.world_from_clip = glm::inverse(view.clip_from_world);
        core::PreviousViewData previous;
        previous.view_from_world = glm::inverse(glm::translate(glm::mat4{1.0f}, glm::vec3{-1, 2, 4}));
        previous.clip_from_view  = view.clip_from_view;
        previous.clip_from_world = view.clip_from_view * previous.view_from_world;
        view_buffer.set(view);
        previous_buffer.set(previous);
        view_buffer.write_buffer(device, queue);
        previous_buffer.write_buffer(device, queue);
        render::ComputePipelineDescriptor descriptor{
            .label       = "view transformations arithmetic oracle",
            .layouts     = {view_layout, output_layout},
            .shader      = shader,
            .shader_defs = pbr::specialize_mesh_capability_shader_defs(false, false, false, std::nullopt),
            .entry_point = "main",
        };
        descriptor.shader_defs.push_back(epix::shader::ShaderDefVal::from_uint("AVAILABLE_STORAGE_BUFFER_BINDINGS", 8));
        if (variant != 0)
            descriptor.shader_defs.push_back(epix::shader::ShaderDefVal::from_bool(
                variant == 1 ? "VIEW_PROJECTION_PERSPECTIVE" : "VIEW_PROJECTION_ORTHOGRAPHIC"));
        const auto pipeline_id = server.queue_compute_pipeline(std::move(descriptor));
        for (int attempt = 0; attempt < 10000 && !server.get_compute_pipeline(pipeline_id); ++attempt) {
            app.run_schedule(epix::app::PreUpdate);
            app.run_schedule(epix::app::PostUpdate);
            app.run_schedule(epix::app::Last);
            std::this_thread::yield();
        }
        const auto pipeline = server.get_compute_pipeline(pipeline_id);
        ASSERT_TRUE(pipeline);
        const auto uniforms_group = device.createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLayout(server.get_bind_group_layout(view_layout))
                .setEntries(std::array{
                    wgpu::BindGroupEntry().setBinding(0).setBuffer(*view_buffer.buffer()).setSize(sizeof(view)),
                    wgpu::BindGroupEntry()
                        .setBinding(2)
                        .setBuffer(*previous_buffer.buffer())
                        .setSize(sizeof(previous))}));
        const auto output_group = device.createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLayout(server.get_bind_group_layout(output_layout))
                .setEntries(
                    std::array{wgpu::BindGroupEntry().setBinding(8).setBuffer(output).setSize(sizeof(Values)),
                               wgpu::BindGroupEntry().setBinding(9).setBuffer(input_buffer).setSize(sizeof(inputs))}));
        const auto encoder = device.createCommandEncoder();
        const auto pass    = encoder.beginComputePass();
        pass.setPipeline(pipeline->get().pipeline());
        pass.setBindGroup(0, uniforms_group, std::span<const std::uint32_t>{});
        pass.setBindGroup(1, output_group, std::span<const std::uint32_t>{});
        pass.dispatchWorkgroups(1, 1, 1);
        pass.end();
        queue.submit(encoder.finish());

        const auto p          = inputs[0];
        const auto d          = inputs[1];
        const auto c          = inputs[2];
        const auto xyz        = [](glm::vec4 v, float w = 1.0f) { return glm::vec4{glm::vec3(v), w}; };
        const auto ndc        = [](glm::vec4 v) { return glm::vec4{glm::vec3(v) / v.w, 1.0f}; };
        const auto depth_view = view.view_from_clip * glm::vec4{0, 0, 0.4f, 1};
        const auto depth_clip = view.clip_from_view * glm::vec4{0, 0, -3, 1};
        const Values expected{
            xyz(view.world_from_view * p),
            xyz(view.world_from_clip * c),
            ndc(view.world_from_clip * c),
            xyz(view.world_from_view * d, 0),
            xyz(view.world_from_clip * d, 0),
            xyz(view.view_from_world * p),
            xyz(view.view_from_clip * c),
            ndc(view.view_from_clip * c),
            xyz(view.view_from_world * d, 0),
            xyz(view.view_from_clip * d, 0),
            xyz(previous.view_from_world * p),
            ndc(previous.clip_from_world * p),
            view.clip_from_world * p,
            view.clip_from_view * p,
            view.clip_from_world * d,
            view.clip_from_view * d,
            ndc(view.clip_from_world * p),
            ndc(view.clip_from_view * p),
            glm::vec4{view.clip_from_view[3][2], depth_view.z / depth_view.w, depth_clip.z / depth_clip.w,
                      depth_clip.z / depth_clip.w},
        };
        const auto actual = read_gpu_value<Values>(output);
        for (std::size_t index = 0; index < expected.size(); ++index) {
            SCOPED_TRACE(index);
            for (int component = 0; component < 4; ++component)
                EXPECT_NEAR(actual[index][component], expected[index][component], 1e-4f);
        }
    }
}

TEST_F(MeshViewPrepare, MeshAndSkinShaderArithmeticMatchesUploadedTransforms) {
    app.add_plugins(epix::mesh::MeshPlugin{}, epix::transform::TransformPlugin{});
    pbr::register_mesh_shaders(app.world_mut());
    app.add_plugins(render::FrameCountPlugin{});
    static constexpr std::string_view source = R"slang(
import epix.mesh_functions;
import epix.mesh_bindings;
import epix.skinning;
using namespace epix::mesh_functions;
[[vk::binding(8, 1)]] RWStructuredBuffer<float4> results;
struct ProbeInputs {
    float4 position;
    float4 normal;
    float4 tangent;
    float4 weights;
    float4 zero;
    uint4 joints;
    uint4 metadata;
};
[[vk::binding(9, 1)]] ConstantBuffer<ProbeInputs> inputs;
[shader("compute")]
[numthreads(1, 1, 1)]
void main() {
    uint instance = inputs.metadata.x;
    float4x4 model = get_world_from_local(instance);
    float4x4 previous = get_previous_world_from_local(instance);
    float4x4 inverse_model = get_local_from_world(instance);
    float4x4 skin = skin_model(inputs.joints, inputs.weights, instance);
    float4x4 previous_skin = skin_prev_model(inputs.joints, inputs.weights, instance);
    results[0] = mesh_position_local_to_world(model, inputs.position);
    results[1] = mesh_position_local_to_world(previous, inputs.position);
    results[2] = mesh_position_local_to_world(inverse_model, inputs.position);
    results[3] = mesh_position_local_to_world(inverse_model, results[0]);
    results[4] = float4(mesh_normal_local_to_world(inputs.normal.xyz, instance), 0);
    results[5] = float4(mesh_normal_local_to_world(inputs.zero.xyz, instance), 0);
    results[6] = mesh_tangent_local_to_world(model, inputs.tangent, instance);
    results[7] = mesh_tangent_local_to_world(model, inputs.zero, instance);
    results[8] = mesh_position_local_to_clip(model, inputs.position);
    results[9] = mesh_position_local_to_world(skin, inputs.position);
    results[10] = mesh_position_local_to_world(previous_skin, inputs.position);
    results[11] = float4(skin_normals(skin, inputs.normal.xyz), 0);
    results[12] = float4(skin_normals(previous_skin, inputs.normal.xyz), 0);
    results[13] = mesh_tangent_local_to_world(skin, inputs.tangent, instance);
    results[14] = mesh_position_local_to_clip(skin, inputs.position);
    results[15] = float4(sign_determinant_model_3x3m(mesh_instance(instance).flags),
        float(get_tag(instance)), float(mesh_instance(instance).first_vertex_index),
        float(mesh_instance(instance).current_skin_index));
    results[16] = float4(
        float(get_visibility_range_dither_level(instance, transpose(model)[3])),
        float(get_visibility_range_dither_level(instance, inputs.zero)),
        float(get_visibility_range_dither_level(instance, inputs.position)),
        float(get_visibility_range_dither_level(instance, inputs.position * 10)));
}
)slang";
    app.resource_mut<epix::assets::EmbeddedAssetRegistry>().insert_asset_static(
        "tests/pbr/mesh_skin_oracle.slang",
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(source.data()), source.size()});
    const auto& assets = app.resource<epix::assets::AssetServer>();
    const auto shader = assets.load<epix::shader::Shader>("embedded://tests/pbr/mesh_skin_oracle.slang");
    const auto& handles = app.resource<pbr::MeshShaderHandles>();
    const std::array dependencies{
        assets.load<epix::shader::Shader>("embedded://epix/shaders/view.slang"),
        assets.load<epix::shader::Shader>("embedded://epix/shaders/globals.slang"),
        assets.load<epix::shader::Shader>("embedded://epix/shaders/maths.slang"),
        handles.mesh_bindings, handles.mesh_functions, handles.skinning, shader};
    const auto loaded = [&] {
        return std::ranges::all_of(dependencies, [&](const auto& handle) {
            return assets.is_loaded_with_dependencies(handle.id());
        });
    };
    const auto pump = [&] {
        app.run_schedule(epix::app::PreUpdate);
        app.run_schedule(epix::app::PostUpdate);
        app.run_schedule(epix::app::Last);
        std::this_thread::yield();
    };
    for (int attempt = 0; attempt < 10000 && !loaded(); ++attempt) pump();
    ASSERT_TRUE(loaded());

    const auto& device = world->resource<wgpu::Device>();
    const auto& queue = world->resource<wgpu::Queue>();
    auto& server = app.resource<render::PipelineServer>();
    wgpu::Limits limits{};
    ASSERT_EQ(device.getLimits(&limits), wgpu::Status::eSuccess);
    const auto buffer_entry = [](std::uint32_t binding, wgpu::BufferBindingType type, std::uint64_t size) {
        return wgpu::BindGroupLayoutEntry().setBinding(binding).setVisibility(wgpu::ShaderStage::eCompute)
            .setBuffer(wgpu::BufferBindingLayout().setType(type).setMinBindingSize(size));
    };
    const auto upload = [&](std::span<const std::byte> bytes, wgpu::BufferUsage usage) {
        const auto buffer = device.createBuffer(wgpu::BufferDescriptor().setSize(bytes.size())
                                                    .setUsage(usage | wgpu::BufferUsage::eCopyDst));
        queue.writeBuffer(buffer, 0, bytes.data(), bytes.size());
        return buffer;
    };
    struct Inputs {
        glm::vec4 position{0.25f, -0.5f, -2.0f, 1};
        glm::vec4 normal{0.3f, 0.7f, 0.2f, 0};
        glm::vec4 tangent{0.8f, -0.2f, 0.4f, -1};
        glm::vec4 weights{0.1f, 0.2f, 0.3f, 0.4f};
        glm::vec4 zero{0.0f};
        glm::uvec4 joints{3, 1, 0, 2};
        glm::uvec4 metadata{1, 0, 0, 0};
    };
    static_assert(sizeof(Inputs) == 7 * sizeof(glm::vec4));
    const std::array inputs{Inputs{}};
    const auto input_buffer = upload(std::as_bytes(std::span{inputs}), wgpu::BufferUsage::eUniform);
    using Values = std::array<glm::vec4, 17>;
    const auto output = device.createBuffer(wgpu::BufferDescriptor().setSize(sizeof(Values))
                                               .setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopySrc));
    render::view::ViewUniform view{};
    view.world_position = {0, 0, 0};
    view.clip_from_world = epix::camera::perspective_infinite_reverse_rh(1.1f, 1.5f, 0.3f) *
                          glm::inverse(glm::translate(glm::mat4{1.0f}, glm::vec3{2, -3, 7}) *
                                       glm::rotate(glm::mat4{1.0f}, 0.3f, glm::vec3{0, 1, 0}));
    render::render_resource::UniformBuffer<render::view::ViewUniform> view_buffer;
    view_buffer.set(view);
    view_buffer.write_buffer(device, queue);
    const std::array visibility_ranges{glm::vec4{0, 2, 4, 8}};
    const auto visibility_buffer = upload(std::as_bytes(std::span{visibility_ranges}), wgpu::BufferUsage::eStorage);
    const auto view_layout = render::BindGroupLayoutDescriptor::create(
        "mesh oracle view", std::array{buffer_entry(0, wgpu::BufferBindingType::eUniform, sizeof(view)),
                                        buffer_entry(14, wgpu::BufferBindingType::eReadOnlyStorage, sizeof(visibility_ranges))});
    const auto output_layout = render::BindGroupLayoutDescriptor::create(
        "mesh oracle output", std::array{buffer_entry(8, wgpu::BufferBindingType::eStorage, sizeof(Values)),
                                          buffer_entry(9, wgpu::BufferBindingType::eUniform, sizeof(inputs))});

    for (int variant = 0; variant < 4; ++variant) {
        const bool uniform = variant >= 2;
        const bool mirrored = variant % 2 != 0;
        SCOPED_TRACE(uniform ? "uniform mesh and skin buffers" : "storage mesh and skin buffers");
        SCOPED_TRACE(mirrored ? "negative determinant" : "positive determinant");
        const float sign = mirrored ? -1.0f : 1.0f;
        const glm::mat4 model = glm::translate(glm::mat4{1.0f}, glm::vec3{3, -2, 5}) *
            glm::rotate(glm::mat4{1.0f}, 0.45f, glm::normalize(glm::vec3{1, 2, 3})) *
            glm::scale(glm::mat4{1.0f}, glm::vec3{sign * 1.7f, 0.6f, 2.3f});
        const glm::mat4 previous = glm::translate(glm::mat4{1.0f}, glm::vec3{-2, 4, -1}) *
            glm::rotate(glm::mat4{1.0f}, -0.4f, glm::vec3{1, 0, 0}) *
            glm::scale(glm::mat4{1.0f}, glm::vec3{0.9f, 1.3f, 2.2f});
        const std::array meshes{
            pbr::MeshUniform::make({}, 0, {}, std::nullopt, std::nullopt, std::nullopt),
            pbr::MeshUniform::make({.world_from_local = model, .previous_world_from_local = previous,
                                    .flags = mirrored ? 0u : pbr::MeshFlags::SIGN_DETERMINANT_MODEL_3X3},
                                   17, {}, std::nullopt, 2u, 13u)};
        const auto joint_offset = uniform ? std::max<std::uint64_t>(limits.minUniformBufferOffsetAlignment,
                                                                    sizeof(glm::mat4)) : 0u;
        const std::size_t prefix = uniform ? joint_offset / sizeof(glm::mat4) : 2;
        const std::uint64_t joint_binding_size = (uniform ? pbr::MAX_JOINTS : 6) * sizeof(glm::mat4);
        std::vector<glm::mat4> joints(uniform ? prefix + pbr::MAX_JOINTS : 6, glm::mat4{1.0f});
        auto previous_joints = joints;
        for (std::size_t index = 0; index < 4; ++index) {
            const float i = static_cast<float>(index);
            joints[prefix + index] = glm::translate(glm::mat4{1.0f}, glm::vec3{1 + i * 0.4f, 2 - i * 0.3f, -i}) *
                glm::rotate(glm::mat4{1.0f}, 0.2f + i * 0.13f, glm::normalize(glm::vec3{1, 2, 1})) *
                glm::scale(glm::mat4{1.0f}, glm::vec3{sign * (1 + i * 0.1f), 0.7f + i * 0.15f, 1.4f - i * 0.08f});
            previous_joints[prefix + index] =
                glm::translate(glm::mat4{1.0f}, glm::vec3{-i, 1 + i * 0.2f, 3 - i * 0.4f}) *
                glm::rotate(glm::mat4{1.0f}, -0.1f - i * 0.2f, glm::normalize(glm::vec3{2, 1, 3})) *
                glm::scale(glm::mat4{1.0f}, glm::vec3{sign * 0.8f, 1.2f + i * 0.1f, 0.9f});
        }
        glm::mat4 skin{0.0f}, previous_skin{0.0f};
        for (int index = 0; index < 4; ++index) {
            skin += inputs[0].weights[index] * joints[prefix + inputs[0].joints[index]];
            previous_skin += inputs[0].weights[index] * previous_joints[prefix + inputs[0].joints[index]];
        }
        const auto usage = uniform ? wgpu::BufferUsage::eUniform : wgpu::BufferUsage::eStorage;
        const auto type = uniform ? wgpu::BufferBindingType::eUniform : wgpu::BufferBindingType::eReadOnlyStorage;
        const auto mesh_buffer = upload(std::as_bytes(std::span{meshes}), usage);
        const auto joint_buffer = upload(std::as_bytes(std::span{joints}), usage);
        const auto previous_joint_buffer = upload(std::as_bytes(std::span{previous_joints}), usage);
        const auto mesh_layout = render::BindGroupLayoutDescriptor::create(
            uniform ? "mesh oracle uniform buffers" : "mesh oracle storage buffers",
            std::array{buffer_entry(0, type, sizeof(meshes)), buffer_entry(1, type, joint_binding_size),
                       buffer_entry(6, type, joint_binding_size)});
        render::ComputePipelineDescriptor descriptor{
            .label = "mesh and skin arithmetic oracle",
            .layouts = {view_layout, output_layout, mesh_layout},
            .shader = shader,
            .shader_defs = pbr::specialize_mesh_capability_shader_defs(false, false, false, std::nullopt),
            .entry_point = "main",
        };
        descriptor.shader_defs.push_back(epix::shader::ShaderDefVal::from_bool("SKINNED"));
        descriptor.shader_defs.push_back(epix::shader::ShaderDefVal::from_bool("VISIBILITY_RANGE_DITHER"));
        descriptor.shader_defs.push_back(epix::shader::ShaderDefVal::from_uint("AVAILABLE_STORAGE_BUFFER_BINDINGS", 8));
        if (uniform) {
            descriptor.shader_defs.push_back(epix::shader::ShaderDefVal::from_bool("SKINS_USE_UNIFORM_BUFFERS"));
            descriptor.shader_defs.push_back(epix::shader::ShaderDefVal::from_uint("PER_OBJECT_BUFFER_BATCH_SIZE", 2));
        }
        // A non-null native pipeline can still be an invalid WebGPU object.
        // Fail at creation instead of submitting it and triggering a native abort.
        device.pushErrorScope(wgpu::ErrorFilter::eValidation);
        const auto id = server.queue_compute_pipeline(std::move(descriptor));
        for (int attempt = 0; attempt < 10000 && !server.get_compute_pipeline(id); ++attempt) pump();
        struct Validation {
            bool completed = false;
            wgpu::PopErrorScopeStatus status{};
            wgpu::ErrorType type{};
            std::string message;
        };
        const auto validation = std::make_shared<Validation>();
        device.popErrorScope(wgpu::PopErrorScopeCallbackInfo().setMode(wgpu::CallbackMode::eAllowProcessEvents)
            .setCallback(wgpu::PopErrorScopeCallback([validation](wgpu::PopErrorScopeStatus status,
                                                                 wgpu::ErrorType type, wgpu::StringView message) {
                validation->status = status;
                validation->type = type;
                if (type != wgpu::ErrorType::eNoError) validation->message = std::string(std::string_view(message));
                validation->completed = true;
            })));
        for (int attempt = 0; attempt < 1000 && !validation->completed; ++attempt) device.poll(wgpu::Bool(true));
        ASSERT_TRUE(validation->completed);
        ASSERT_EQ(validation->status, wgpu::PopErrorScopeStatus::eSuccess);
        ASSERT_EQ(validation->type, wgpu::ErrorType::eNoError) << validation->message;
        const auto pipeline = server.get_compute_pipeline(id);
        ASSERT_TRUE(pipeline);
        const auto view_group = device.createBindGroup(wgpu::BindGroupDescriptor()
            .setLayout(server.get_bind_group_layout(view_layout))
            .setEntries(std::array{wgpu::BindGroupEntry().setBinding(0).setBuffer(*view_buffer.buffer()).setSize(sizeof(view)),
                                   wgpu::BindGroupEntry().setBinding(14).setBuffer(visibility_buffer).setSize(sizeof(visibility_ranges))}));
        const auto output_group = device.createBindGroup(wgpu::BindGroupDescriptor()
            .setLayout(server.get_bind_group_layout(output_layout))
            .setEntries(std::array{wgpu::BindGroupEntry().setBinding(8).setBuffer(output).setSize(sizeof(Values)),
                                   wgpu::BindGroupEntry().setBinding(9).setBuffer(input_buffer).setSize(sizeof(inputs))}));
        const auto mesh_group = device.createBindGroup(wgpu::BindGroupDescriptor()
            .setLayout(server.get_bind_group_layout(mesh_layout))
            .setEntries(std::array{wgpu::BindGroupEntry().setBinding(0).setBuffer(mesh_buffer).setSize(sizeof(meshes)),
                                   wgpu::BindGroupEntry().setBinding(1).setBuffer(joint_buffer).setOffset(joint_offset)
                                       .setSize(joint_binding_size),
                                   wgpu::BindGroupEntry().setBinding(6).setBuffer(previous_joint_buffer).setOffset(joint_offset)
                                       .setSize(joint_binding_size)}));
        const auto encoder = device.createCommandEncoder();
        const auto pass = encoder.beginComputePass();
        pass.setPipeline(pipeline->get().pipeline());
        pass.setBindGroup(0, view_group, std::span<const std::uint32_t>{});
        pass.setBindGroup(1, output_group, std::span<const std::uint32_t>{});
        pass.setBindGroup(2, mesh_group, std::span<const std::uint32_t>{});
        pass.dispatchWorkgroups(1, 1, 1);
        pass.end();
        queue.submit(encoder.finish());

        const auto normal = [&](const glm::mat4& matrix) {
            return glm::vec4{glm::normalize(glm::transpose(glm::inverse(glm::mat3{matrix})) * glm::vec3{inputs[0].normal}), 0};
        };
        const auto tangent = [&](const glm::mat4& matrix) {
            return glm::vec4{glm::normalize(glm::mat3{matrix} * glm::vec3{inputs[0].tangent}), inputs[0].tangent.w * sign};
        };
        const auto p = inputs[0].position;
        const Values expected{model * p, previous * p, glm::inverse(model) * p, p,
                              normal(model), glm::vec4{0.0f}, tangent(model), glm::vec4{0.0f},
                              view.clip_from_world * model * p, skin * p, previous_skin * p,
                              normal(skin), normal(previous_skin), tangent(skin),
                              view.clip_from_world * skin * p, glm::vec4{sign, 13, 17, 2}, glm::vec4{9, -16, 0, 16}};
        constexpr std::array names{"mesh position", "previous mesh position", "inverse mesh position", "mesh roundtrip",
                                   "mesh normal", "zero normal", "mesh tangent", "zero tangent", "mesh clip position",
                                   "skin position", "previous skin position", "skin normal", "previous skin normal",
                                   "skin tangent", "skin clip position", "instance fields", "visibility dither"};
        const auto actual = read_gpu_value<Values>(output);
        for (std::size_t index = 0; index < expected.size(); ++index) {
            SCOPED_TRACE(names[index]);
            for (int component = 0; component < 4; ++component)
                EXPECT_NEAR(actual[index][component], expected[index][component], 2e-4f);
        }
    }
}

}  // namespace
