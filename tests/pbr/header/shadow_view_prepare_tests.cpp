#include <gtest/gtest.h>

#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/pbr/light_meta.hpp>
#include <epix/pbr/pbr_plugin.hpp>
#include <epix/pbr/shadow_phase.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace {
namespace ecs = epix::ecs;
namespace pbr = epix::pbr;
namespace render = epix::render;
namespace camera = epix::camera;
using MainEntity = render::sync_world::MainEntity;
using Phases = render::phase::ViewBinnedRenderPhases<pbr::Shadow>;

struct IoTaskPoolInit {
    IoTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPoolBuilder{}.num_threads(4).build());
    }
} io_task_pool_init;

class ShadowViewPrepare : public testing::Test {
   protected:
    epix::app::App app = epix::app::App::create();
    ecs::World* world = nullptr;

    void SetUp() override {
        app.add_events<epix::window::WindowClosed>();
        app.add_plugins(epix::time::TimePlugin{}, camera::CameraPlugin{}, epix::assets::AssetPlugin{},
                        epix::image::ImagePlugin{}, epix::mesh::MeshPlugin{}, epix::transform::TransformPlugin{},
                        epix::shader::ShaderPlugin{}, render::FrameCountPlugin{});
        ASSERT_NO_THROW(app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true}));
        app.add_plugins(epix::core_graph::CoreGraphPlugin{}, pbr::PbrPlugin{.supports_storage_buffers = true});
        app.resource_scope([&](epix::app::internal::Plugins& plugins) { plugins.ready_all(app); });
        auto& render_app = app.get_sub_app_mut(render::Render)->get();
        world = &render_app.world_mut();
        render_app.run_schedule(render::RenderStartup);
        world->insert_resource(pbr::GlobalAmbientLight{});
        world->insert_resource(pbr::PointLightShadowMap{.size = 16});
        world->insert_resource(pbr::DirectionalLightShadowMap{.size = 32});
        world->resource_mut<pbr::LightMeta>().view_gpu_lights.add_usages(wgpu::BufferUsage::eCopySrc);
    }

    template <class F>
    void run(F function) {
        auto system = ecs::make_system_unique(function);
        system->initialize(*world);
        ASSERT_TRUE(system->run({}, *world).has_value());
        system->apply_deferred(*world);
    }

    void prepare() {
        world->flush();
        run(pbr::prepare_lights_shadow_bindings);
        run(pbr::prepare_clusterable_objects);
        run(pbr::prepare_view_lights);
        world->flush();
    }

    ecs::Entity add_camera(std::uint32_t main_index, bool indirect = true) {
        const auto main = MainEntity{ecs::Entity::from_index(main_index)};
        const auto entity = world->spawn(camera::Camera3d{}, main, pbr::ExtractedClusterConfig{},
            render::view::ExtractedView{
                .retained_view_entity = render::view::RetainedViewEntity{main},
                .clip_from_view = glm::mat4{1.0f},
                .world_from_view = {}, .viewport = {0, 0, 64, 32}}).id();
        if (!indirect) world->entity_mut(entity).insert(render::view::NoIndirectDrawing{});
        world->resource_mut<render::camera::SortedCameras>().cameras.push_back({.entity = entity});
        return entity;
    }

    ecs::Entity add_point(std::uint32_t main_index, float near_z, bool spot = false) {
        pbr::ExtractedPointLight light;
        light.shadows_enabled = true;
        light.shadow_map_near_z = near_z;
        light.range = 50.0f;
        light.transform[0] *= 2.0f;
        light.transform[1] *= 3.0f;
        light.transform[3] = {3, 4, 5, 1};
        if (spot) light.spot_light_angles = std::pair{0.2f, 0.5f};
        const auto entity = world->spawn(std::move(light), MainEntity{ecs::Entity::from_index(main_index)}).id();
        if (spot) world->entity_mut(entity).insert(camera::Frustum{});
        else world->entity_mut(entity).insert(camera::CubemapFrusta{});
        return entity;
    }

    ecs::Entity add_directional(std::uint32_t main_index, const std::vector<ecs::Entity>& cameras,
                                std::size_t count = 2, bool occlusion = false) {
        pbr::ExtractedDirectionalLight light;
        light.shadows_enabled = true;
        light.occlusion_culling = occlusion;
        light.cascade_shadow_config.bounds.clear();
        for (std::size_t i = 0; i < count; ++i) light.cascade_shadow_config.bounds.push_back(17.0f + i * 11.0f);
        for (const auto view : cameras) {
            for (std::size_t i = 0; i < count; ++i) {
                pbr::Cascade cascade;
                cascade.world_from_cascade[3] = {float(i), 2, 3, 1};
                cascade.clip_from_cascade[0][0] = 0.5f + float(i);
                cascade.clip_from_world = cascade.clip_from_cascade * glm::inverse(cascade.world_from_cascade);
                cascade.texel_size = 0.125f * (i + 1);
                light.cascades[view].push_back(cascade);
                light.frusta[view].push_back(camera::Frustum::from_clip_from_world(cascade.clip_from_world));
            }
        }
        return world->spawn(std::move(light), MainEntity{ecs::Entity::from_index(main_index)}).id();
    }

    const std::vector<ecs::Entity>& light_views(ecs::Entity light, ecs::Entity view) {
        return world->entity(light).get<pbr::LightViewEntities>()->get().entities.at(view);
    }

    template <class T>
    const T& get(ecs::Entity entity) { return world->entity(entity).get<T>()->get(); }

    pbr::GpuLights uploaded_lights(ecs::Entity view) {
        const auto& device = world->resource<wgpu::Device>();
        const auto staging = device.createBuffer(wgpu::BufferDescriptor{}.setSize(sizeof(pbr::GpuLights))
            .setUsage(wgpu::BufferUsage::eCopyDst | wgpu::BufferUsage::eMapRead));
        const auto encoder = device.createCommandEncoder();
        encoder.copyBufferToBuffer(*world->resource<pbr::LightMeta>().view_gpu_lights.buffer(),
            get<pbr::ViewLightsUniformOffset>(view).offset, staging, 0, sizeof(pbr::GpuLights));
        world->resource<wgpu::Queue>().submit(encoder.finish());
        struct Result { pbr::GpuLights lights{}; bool completed = false; bool mapped = false; };
        const auto result = std::make_shared<Result>();
        staging.mapAsync(wgpu::MapMode::eRead, 0, sizeof(pbr::GpuLights), wgpu::BufferMapCallbackInfo{}
            .setMode(wgpu::CallbackMode::eAllowProcessEvents)
            .setCallback(wgpu::BufferMapCallback{[result, staging](wgpu::MapAsyncStatus status, wgpu::StringView) {
                result->completed = true;
                if (status != wgpu::MapAsyncStatus::eSuccess) return;
                std::memcpy(&result->lights, staging.getConstMappedRange(0, sizeof(pbr::GpuLights)), sizeof(pbr::GpuLights));
                staging.unmap();
                result->mapped = true;
            }}));
        for (unsigned attempt = 0; attempt < 1000 && !result->completed; ++attempt) device.poll(wgpu::Bool{true});
        EXPECT_TRUE(result->mapped);
        return result->lights;
    }
};

TEST_F(ShadowViewPrepare, ProducesAllLightKindsWithMainWorldRetainedIdentities) {
    const auto view = add_camera(4000);
    const auto point = add_point(5000, 0.2f);
    const auto spot = add_point(5001, 0.7f, true);
    const auto directional = add_directional(5002, {view});
    prepare();
    const auto list = world->entity(view).get<pbr::ViewLightEntities>();
    ASSERT_TRUE(list);
    EXPECT_EQ(list->get().lights.size(), 9u);
    ASSERT_EQ(light_views(point, view).size(), 6u);
    ASSERT_EQ(light_views(spot, view).size(), 1u);
    ASSERT_EQ(light_views(directional, view).size(), 2u);
    for (const auto light : {point, spot, directional}) {
        const auto& entities = light_views(light, view);
        for (std::size_t i = 0; i < entities.size(); ++i) {
            const auto& extracted = get<render::view::ExtractedView>(entities[i]);
            EXPECT_EQ(extracted.retained_view_entity.main_entity, get<MainEntity>(light));
            EXPECT_EQ(extracted.retained_view_entity.auxiliary_entity, get<MainEntity>(view));
            EXPECT_EQ(extracted.retained_view_entity.subview_index, i);
            EXPECT_EQ(get<pbr::LightEntity>(entities[i]).light_entity, light);
            EXPECT_TRUE(world->entity(entities[i]).contains<pbr::ShadowView>());
            EXPECT_TRUE(world->entity(entities[i]).contains<camera::Frustum>());
        }
    }
    EXPECT_EQ(world->resource<Phases>().phases.size(), 9u);
}

TEST_F(ShadowViewPrepare, PointAndSpotProjectionsUseEachLightsNearPlane) {
    const auto view = add_camera(4000);
    const auto first = add_point(5000, 0.2f);
    const auto second = add_point(5001, 1.7f);
    const auto spot = add_point(5002, 0.6f, true);
    prepare();
    ASSERT_TRUE(world->entity(view).contains<pbr::ViewLightEntities>());
    for (const auto light : {first, second}) {
        for (const auto shadow : light_views(light, view)) {
            const auto& extracted = get<render::view::ExtractedView>(shadow);
            EXPECT_FLOAT_EQ(extracted.clip_from_view[3][2], get<pbr::ExtractedPointLight>(light).shadow_map_near_z);
            EXPECT_EQ(glm::vec3(extracted.world_from_view.matrix[3]), (glm::vec3{3, 4, 5}));
            EXPECT_FLOAT_EQ(glm::length(glm::vec3(extracted.world_from_view.matrix[0])), 1.0f);
            EXPECT_FALSE(extracted.clip_from_world.has_value());
        }
    }
    const auto& extracted = get<render::view::ExtractedView>(light_views(spot, view)[0]);
    EXPECT_FLOAT_EQ(extracted.clip_from_view[3][2], 0.6f);
    EXPECT_EQ(extracted.viewport, (glm::uvec4{0, 0, 32, 32}));
}

TEST_F(ShadowViewPrepare, SortedFirstCameraOwnsSharedPhasesButDirectionalAttachmentsAreIndependent) {
    const auto first = add_camera(4000);
    const auto second = add_camera(4001, false);
    std::ranges::reverse(world->resource_mut<render::camera::SortedCameras>().cameras);
    const auto point = add_point(5000, 0.2f);
    const auto spot = add_point(5001, 0.6f, true);
    const auto directional = add_directional(5002, {first, second});
    prepare();
    ASSERT_TRUE(world->entity(first).contains<pbr::ViewLightEntities>());
    ASSERT_TRUE(world->entity(second).contains<pbr::ViewLightEntities>());
    for (const auto light : {point, spot}) {
        for (std::size_t i = 0; i < light_views(light, first).size(); ++i) {
            const auto a = light_views(light, first)[i];
            const auto b = light_views(light, second)[i];
            const auto& attachment_a = get<pbr::ShadowView>(a).depth_attachment;
            const auto& attachment_b = get<pbr::ShadowView>(b).depth_attachment;
            EXPECT_EQ(attachment_a.view, attachment_b.view);
            EXPECT_EQ(attachment_a.is_first_call, attachment_b.is_first_call);
            EXPECT_EQ(attachment_b.get_attachment(wgpu::StoreOp::eStore).depthLoadOp, wgpu::LoadOp::eClear);
            EXPECT_EQ(attachment_a.get_attachment(wgpu::StoreOp::eStore).depthLoadOp, wgpu::LoadOp::eLoad);
            EXPECT_FALSE(world->resource<Phases>().phases.contains(get<render::view::ExtractedView>(a).retained_view_entity));
            EXPECT_TRUE(world->resource<Phases>().phases.contains(get<render::view::ExtractedView>(b).retained_view_entity));
        }
    }
    const auto a = light_views(directional, first)[0];
    const auto b = light_views(directional, second)[0];
    EXPECT_NE(get<pbr::ShadowView>(a).depth_attachment.is_first_call,
              get<pbr::ShadowView>(b).depth_attachment.is_first_call);
    EXPECT_EQ(world->resource<Phases>().phases.size(), 11u);
}

TEST_F(ShadowViewPrepare, DirectionalCascadesCarryMatricesBoundsAndInfiniteNearPlaneAndOcclusionLinks) {
    const auto view = add_camera(4000);
    const auto light = add_directional(5000, {view}, 2, true);
    prepare();
    ASSERT_TRUE(world->entity(view).contains<pbr::ViewLightEntities>());
    const auto& original = get<pbr::ExtractedDirectionalLight>(light);
    for (std::size_t i = 0; i < 2; ++i) {
        const auto shadow = light_views(light, view)[i];
        const auto& extracted = get<render::view::ExtractedView>(shadow);
        EXPECT_EQ(extracted.clip_from_world, std::optional{original.cascades.at(view)[i].clip_from_world});
        EXPECT_EQ(extracted.clip_from_view, original.cascades.at(view)[i].clip_from_cascade);
        EXPECT_EQ(get<camera::Frustum>(shadow).planes[camera::Frustum::NEAR_PLANE_IDX].normal(),
                  original.frusta.at(view)[i].planes[camera::Frustum::NEAR_PLANE_IDX].normal());
        EXPECT_TRUE(std::isinf(get<camera::Frustum>(shadow).planes[camera::Frustum::NEAR_PLANE_IDX].normal_d().w));
        EXPECT_TRUE(world->entity(shadow).contains<render::experimental::OcclusionCulling>());
        EXPECT_EQ(get<render::experimental::OcclusionCullingSubview>(shadow).depth_texture_view,
                  get<pbr::ShadowView>(shadow).depth_attachment.view);
        EXPECT_EQ(get<render::experimental::OcclusionCullingSubview>(shadow).depth_texture_size, 32u);
    }
    EXPECT_EQ(get<render::experimental::OcclusionCullingSubviewEntities>(view).entities, light_views(light, view));
}

TEST_F(ShadowViewPrepare, RetainsEntitiesUntilCascadeCountChangesThenRetiresTheWholeOldSet) {
    const auto view = add_camera(4000);
    const auto light = add_directional(5000, {view});
    prepare();
    ASSERT_TRUE(world->entity(view).contains<pbr::ViewLightEntities>());
    const auto before = light_views(light, view);
    prepare();
    EXPECT_EQ(before, light_views(light, view));
    auto changed = get<pbr::ExtractedDirectionalLight>(light);
    changed.cascades.at(view).resize(1);
    changed.frusta.at(view).resize(1);
    changed.cascade_shadow_config.bounds.resize(1);
    world->entity_mut(light).insert(std::move(changed));
    prepare();
    ASSERT_EQ(light_views(light, view).size(), 1u);
    for (const auto old : before) EXPECT_FALSE(world->get_entity(old));
    EXPECT_EQ(world->resource<Phases>().phases.size(), 1u);
}

TEST_F(ShadowViewPrepare, RetiresDisabledLayerExcludedAndRemovedCameraViewsAndPhases) {
    const auto view = add_camera(4000);
    const auto point = add_point(5000, 0.2f);
    const auto spot = add_point(5001, 0.7f, true);
    const auto directional = add_directional(5002, {view});
    prepare();
    ASSERT_TRUE(world->entity(view).contains<pbr::ViewLightEntities>());
    const auto old = get<pbr::ViewLightEntities>(view).lights;
    world->entity_mut(point).get_mut<pbr::ExtractedPointLight>()->get_mut().shadows_enabled = false;
    world->entity_mut(spot).get_mut<pbr::ExtractedPointLight>()->get_mut().shadows_enabled = false;
    world->entity_mut(directional).get_mut<pbr::ExtractedDirectionalLight>()->get_mut().render_layers = camera::RenderLayers::layer(1);
    prepare();
    EXPECT_TRUE(get<pbr::ViewLightEntities>(view).lights.empty());
    for (const auto entity : old) EXPECT_FALSE(world->get_entity(entity));
    EXPECT_TRUE(world->resource<Phases>().phases.empty());
    world->entity_mut(point).get_mut<pbr::ExtractedPointLight>()->get_mut().shadows_enabled = true;
    prepare();
    const auto restored = light_views(point, view);
    world->entity_mut(view).despawn();
    world->resource_mut<render::camera::SortedCameras>().cameras.clear();
    prepare();
    for (const auto entity : restored) EXPECT_FALSE(world->get_entity(entity));
    EXPECT_TRUE(get<pbr::LightViewEntities>(point).entities.empty());
    EXPECT_TRUE(world->resource<Phases>().phases.empty());
}

TEST_F(ShadowViewPrepare, RejectsNonCameraAndUnsortedViews) {
    const auto eligible = add_camera(4000);
    const auto unsorted = add_camera(4001);
    world->resource_mut<render::camera::SortedCameras>().cameras.pop_back();
    const auto not_camera = add_camera(4002);
    world->entity_mut(not_camera).remove<camera::Camera3d>();
    add_point(5000, 0.2f);
    prepare();
    EXPECT_TRUE(world->entity(eligible).contains<pbr::ViewLightEntities>());
    EXPECT_FALSE(world->entity(unsorted).contains<pbr::ViewLightsUniformOffset>());
    EXPECT_FALSE(world->entity(not_camera).contains<pbr::ViewLightsUniformOffset>());
}

TEST_F(ShadowViewPrepare, UploadedCascadeRecordsAndSpotOffsetsUseTheSharedAllocationAcrossCameraLayers) {
    const auto first = add_camera(4000);
    const auto second = add_camera(4001);
    world->entity_mut(second).insert(camera::RenderLayers::layer(1));
    add_point(5000, 0.2f);
    add_point(5001, 0.4f, true);
    const auto a = add_directional(5002, {first, second}, 2);
    const auto b = add_directional(5003, {first, second}, 3);
    world->entity_mut(b).get_mut<pbr::ExtractedDirectionalLight>()->get_mut().render_layers = camera::RenderLayers::layer(1);
    prepare();
    for (const auto& [view, light, count] : std::array{std::tuple{first, a, 2u}, std::tuple{second, b, 3u}}) {
        ASSERT_TRUE(world->entity(view).contains<pbr::ViewLightsUniformOffset>());
        const auto gpu = uploaded_lights(view);
        ASSERT_EQ(gpu.n_directional_lights, 1u);
        EXPECT_EQ(gpu.spot_light_shadowmap_offset, 2);  // max(2,3) - one point, for BOTH cameras.
        EXPECT_EQ(gpu.directional_lights[0].num_cascades, count);
        const auto& source = get<pbr::ExtractedDirectionalLight>(light);
        for (unsigned i = 0; i < count; ++i) {
            EXPECT_EQ(gpu.directional_lights[0].cascades[i].clip_from_world, source.cascades.at(view)[i].clip_from_world);
            EXPECT_FLOAT_EQ(gpu.directional_lights[0].cascades[i].texel_size, source.cascades.at(view)[i].texel_size);
            EXPECT_FLOAT_EQ(gpu.directional_lights[0].cascades[i].far_bound, source.cascade_shadow_config.bounds[i]);
        }
    }
    EXPECT_LT(get<pbr::ViewLightsUniformOffset>(first).offset, get<pbr::ViewLightsUniformOffset>(second).offset);
}

TEST_F(ShadowViewPrepare, PhaseModesRespectDeviceSupportAndEachCameraWithoutRunningCompute) {
    using Mode = render::batching::GpuPreprocessingMode;
    world->insert_resource(render::batching::GpuPreprocessingSupport{.max_supported_mode = Mode::Culling});
    const auto first = add_camera(4000);
    const auto second = add_camera(4001, false);
    const auto light = add_directional(5000, {first, second}, 1);
    prepare();
    ASSERT_TRUE(world->entity(first).contains<pbr::ViewLightEntities>());
    const auto a = light_views(light, first)[0];
    const auto b = light_views(light, second)[0];
    const auto& phases = world->resource<Phases>().phases;
    EXPECT_EQ(phases.at(get<render::view::ExtractedView>(a).retained_view_entity).gpu_preprocessing_mode, Mode::Culling);
    EXPECT_EQ(phases.at(get<render::view::ExtractedView>(b).retained_view_entity).gpu_preprocessing_mode, Mode::PreprocessingOnly);
    EXPECT_FALSE(world->entity(a).contains<render::view::NoIndirectDrawing>());
    EXPECT_TRUE(world->entity(b).contains<render::view::NoIndirectDrawing>());
}

TEST_F(ShadowViewPrepare, CascadeCountIsCappedAndUnqualifiedExtractedLightsAreIgnoredByAllProducers) {
    const auto view = add_camera(4000);
    const auto light = add_directional(5000, {view}, 7);
    pbr::ExtractedPointLight invalid;
    invalid.shadows_enabled = true;
    world->spawn(invalid, camera::CubemapFrusta{});  // Missing MainEntity.
    world->spawn(invalid, MainEntity{ecs::Entity::from_index(5001)});  // Missing both frusta.
    world->spawn(pbr::ExtractedDirectionalLight{.shadows_enabled = true});  // Missing MainEntity.
    prepare();
    ASSERT_TRUE(world->entity(view).contains<pbr::ViewLightEntities>());
    EXPECT_EQ(light_views(light, view).size(), pbr::MAX_CASCADES_PER_LIGHT);
    EXPECT_EQ(world->resource<Phases>().phases.size(), pbr::MAX_CASCADES_PER_LIGHT);
    EXPECT_TRUE(world->resource<pbr::GlobalClusterableObjectMeta>().entity_to_index.empty());
    EXPECT_EQ(get<pbr::ViewShadowBindings>(view).point_light_depth_texture.getDepthOrArrayLayers(), 6u);
    EXPECT_EQ(get<pbr::ViewShadowBindings>(view).directional_light_depth_texture.getDepthOrArrayLayers(), 4u);
}
}  // namespace
