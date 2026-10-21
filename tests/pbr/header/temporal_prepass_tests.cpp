#include <gtest/gtest.h>

#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/pbr/light_meta.hpp>
#include <epix/pbr/material.hpp>
#include <epix/pbr/pbr_plugin.hpp>
#include <epix/pbr/prepass.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

namespace {
namespace ecs = epix::ecs;
namespace pbr = epix::pbr;
namespace render = epix::render;
namespace core = epix::core_graph;
namespace camera = epix::camera;
using History = core::PreviousViewData;
using Offset = core::PreviousViewUniformOffset;

struct IoTaskPoolInit {
    IoTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPoolBuilder{}.num_threads(4).build());
    }
} io_task_pool_init;

class TemporalPrepass : public testing::Test {
   protected:
    epix::app::App app = epix::app::App::create();
    ecs::World* world = nullptr;
    virtual bool install_pbr() const { return true; }

    void SetUp() override {
        app.add_events<epix::window::WindowClosed>();
        app.add_plugins(epix::time::TimePlugin{}, camera::CameraPlugin{}, epix::assets::AssetPlugin{},
                        epix::image::ImagePlugin{}, epix::mesh::MeshPlugin{}, epix::transform::TransformPlugin{},
                        epix::shader::ShaderPlugin{}, render::FrameCountPlugin{});
        ASSERT_NO_THROW(app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true}));
        app.add_plugins(core::CoreGraphPlugin{});
        if (install_pbr()) app.add_plugins(pbr::PbrPlugin{.supports_storage_buffers = true});
        app.resource_scope([&](epix::app::internal::Plugins& plugins) { plugins.ready_all(app); });
        auto& render_app = app.get_sub_app_mut(render::Render)->get();
        world = &render_app.world_mut();
        render_app.run_schedule(render::RenderStartup);
        world->resource_mut<core::PreviousViewUniforms>().uniforms.add_usages(wgpu::BufferUsage::eCopySrc);
    }

    template <class F>
    void run(F function) {
        auto system = ecs::make_system_unique(function);
        system->initialize(*world);
        ASSERT_TRUE(system->run({}, *world).has_value());
        system->apply_deferred(*world);
    }

    void prepare() { run(pbr::prepare_previous_view_uniforms); }

    void extract() {
        world->insert_resource(epix::app::ExtractedWorld{app.world_mut()});
        ASSERT_TRUE(app.get_sub_app_mut(render::Render)->get().run_schedule(render::ExtractSchedule));
        world->remove_resource<epix::app::ExtractedWorld>();
        app.get_sub_app_mut(render::Render)->get().schedule_scope(render::ExtractSchedule,
            [](ecs::Schedule& schedule, ecs::World& render_world) { schedule.apply_deferred(render_world); });
        world->flush();
    }

    static glm::mat4 transform(float x) {
        glm::mat4 result{1.0f};
        result[0][0] = 2.0f;
        result[1][1] = 4.0f;
        result[2][2] = 8.0f;
        result[3] = {x, -2, 5, 1};
        return result;
    }

    ecs::Entity main_mesh(float x, bool history = false) {
        const auto entity = app.world_mut().spawn(epix::mesh::Mesh3d{
            epix::assets::Handle<epix::mesh::Mesh>{epix::assets::AssetId<epix::mesh::Mesh>::invalid()}},
            epix::transform::GlobalTransform{transform(x)}).id();
        if (history) app.world_mut().entity_mut(entity).insert(pbr::PreviousGlobalTransform{transform(1)});
        return entity;
    }

    static render::view::ExtractedView extracted(std::uint32_t main_index) {
        glm::mat4 projection{1.0f};
        projection[0][0] = 0.5f;
        projection[1][1] = 0.25f;
        projection[2][2] = -0.5f;
        projection[3][2] = 0.75f;
        return {.retained_view_entity = render::view::RetainedViewEntity{
                    render::sync_world::MainEntity{ecs::Entity::from_index(main_index)}},
                .clip_from_view = projection,
                .world_from_view = epix::transform::GlobalTransform{transform(3)},
                .viewport = {0, 0, 32, 32}};
    }

    ecs::Entity view(bool shadow, std::uint32_t main_index = 4000) {
        const auto entity = world->spawn(extracted(main_index)).id();
        if (shadow) {
            const auto depth = world->resource<wgpu::Device>().createTexture(wgpu::TextureDescriptor{}
                .setSize({8, 8, 1}).setFormat(wgpu::TextureFormat::eDepth32Float)
                .setUsage(wgpu::TextureUsage::eRenderAttachment | wgpu::TextureUsage::eTextureBinding));
            world->entity_mut(entity).insert(pbr::ShadowView{
                render::render_resource::DepthAttachment{depth.createView(wgpu::TextureViewDescriptor{}), 0.0f},
                "temporal_test_shadow"});
        } else world->entity_mut(entity).insert(camera::Camera3d{});
        return entity;
    }

    std::pair<ecs::Entity, ecs::Entity> main_camera(bool active, bool with_history = true) {
        const auto render_entity = world->spawn().id();
        camera::Camera camera;
        camera.is_active = active;
        const auto main_entity = app.world_mut().spawn(camera::Camera3d{}, std::move(camera),
            epix::transform::GlobalTransform{transform(7)},
            render::sync_world::RenderEntity{render_entity}).id();
        if (with_history) app.world_mut().entity_mut(main_entity).insert(History{.clip_from_world = glm::mat4{7.0f}});
        return {main_entity, render_entity};
    }

    History uploaded(ecs::Entity entity) {
        const auto offset = world->entity(entity).get<Offset>()->get().offset;
        const auto& device = world->resource<wgpu::Device>();
        const auto staging = device.createBuffer(wgpu::BufferDescriptor{}.setSize(sizeof(History))
            .setUsage(wgpu::BufferUsage::eCopyDst | wgpu::BufferUsage::eMapRead));
        const auto encoder = device.createCommandEncoder();
        encoder.copyBufferToBuffer(*world->resource<core::PreviousViewUniforms>().uniforms.buffer(),
                                   offset, staging, 0, sizeof(History));
        world->resource<wgpu::Queue>().submit(encoder.finish());
        struct Result { History data{}; bool done = false; bool mapped = false; };
        const auto result = std::make_shared<Result>();
        staging.mapAsync(wgpu::MapMode::eRead, 0, sizeof(History), wgpu::BufferMapCallbackInfo{}
            .setMode(wgpu::CallbackMode::eAllowProcessEvents)
            .setCallback(wgpu::BufferMapCallback{[result, staging](wgpu::MapAsyncStatus status, wgpu::StringView) {
                result->done = true;
                if (status != wgpu::MapAsyncStatus::eSuccess) return;
                std::memcpy(&result->data, staging.getConstMappedRange(0, sizeof(History)), sizeof(History));
                staging.unmap();
                result->mapped = true;
            }}));
        for (unsigned attempt = 0; attempt < 1000 && !result->done; ++attempt) device.poll(wgpu::Bool{true});
        EXPECT_TRUE(result->mapped);
        return result->data;
    }
};

TEST_F(TemporalPrepass, NewCameraWithoutHistoryReceivesCurrentMatrixFallback) {
    const auto entity = view(false);
    prepare();
    ASSERT_TRUE(world->entity(entity).contains<Offset>());
    const auto data = uploaded(entity);
    const auto& current = world->entity(entity).get<render::view::ExtractedView>()->get();
    EXPECT_EQ(data.view_from_world, glm::inverse(current.world_from_view.matrix));
    EXPECT_EQ(data.clip_from_world, current.clip_from_view * data.view_from_world);
    EXPECT_EQ(data.clip_from_view, current.clip_from_view);
    EXPECT_EQ(data.world_from_clip, current.world_from_view.matrix * glm::inverse(current.clip_from_view));
    EXPECT_EQ(data.view_from_clip, glm::inverse(current.clip_from_view));
}

TEST_F(TemporalPrepass, ShadowViewWithoutHistoryReceivesCurrentMatrixFallback) {
    const auto entity = view(true);
    prepare();
    ASSERT_TRUE(world->entity(entity).contains<Offset>());
    const auto data = uploaded(entity);
    const auto& current = world->entity(entity).get<render::view::ExtractedView>()->get();
    EXPECT_EQ(data.clip_from_world, current.clip_from_view * glm::inverse(current.world_from_view.matrix));
    EXPECT_EQ(data.world_from_clip, current.world_from_view.matrix * glm::inverse(current.clip_from_view));
}

TEST_F(TemporalPrepass, SavedHistoryTakesPrecedenceForCameraAndShadowViews) {
    const auto camera = view(false);
    const auto shadow = view(true, 4001);
    History history{.view_from_world = glm::mat4{2.0f}, .clip_from_world = glm::mat4{3.0f},
        .clip_from_view = glm::mat4{4.0f}, .world_from_clip = glm::mat4{5.0f}, .view_from_clip = glm::mat4{6.0f}};
    world->entity_mut(camera).insert(history);
    world->entity_mut(shadow).insert(history);
    prepare();
    for (const auto entity : {camera, shadow}) {
        ASSERT_TRUE(world->entity(entity).contains<Offset>());
        const auto data = uploaded(entity);
        EXPECT_EQ(data.view_from_world, history.view_from_world);
        EXPECT_EQ(data.clip_from_world, history.clip_from_world);
        EXPECT_EQ(data.clip_from_view, history.clip_from_view);
        EXPECT_EQ(data.world_from_clip, history.world_from_clip);
        EXPECT_EQ(data.view_from_clip, history.view_from_clip);
    }
}

TEST_F(TemporalPrepass, UnrelatedHistoryAndIncompleteViewsDoNotReceiveOffsetsOrAllocateWriter) {
    const auto unrelated = world->spawn(History{}).id();
    const auto no_marker = world->spawn(extracted(4000), History{}).id();
    const auto no_view = world->spawn(camera::Camera3d{}, History{}).id();
    prepare();
    for (const auto entity : {unrelated, no_marker, no_view}) EXPECT_FALSE(world->entity(entity).contains<Offset>());
    EXPECT_EQ(world->resource<core::PreviousViewUniforms>().uniforms.buffer(), nullptr);
}

TEST_F(TemporalPrepass, RegisteredExtractionCopiesActiveCameraHistory) {
    const auto [main_entity, render_entity] = main_camera(true);
    extract();
    ASSERT_TRUE(world->entity(render_entity).contains<History>());
    EXPECT_EQ(world->entity(render_entity).get<History>()->get().clip_from_world, glm::mat4{7.0f});
}

TEST_F(TemporalPrepass, RegisteredExtractionPreservesActiveRenderHistoryWhenMainHistoryIsMissing) {
    const auto [main_entity, render_entity] = main_camera(true, false);
    world->entity_mut(render_entity).insert(History{.clip_from_world = glm::mat4{3.0f}});
    extract();
    ASSERT_TRUE(world->entity(render_entity).contains<History>());
    EXPECT_EQ(world->entity(render_entity).get<History>()->get().clip_from_world, glm::mat4{3.0f});
}

TEST_F(TemporalPrepass, RegisteredExtractionInactiveCameraRemovesHistoryEvenWhenMainHistoryIsMissing) {
    const auto [main_entity, render_entity] = main_camera(false, false);
    world->entity_mut(render_entity).insert(History{});
    extract();
    EXPECT_FALSE(world->entity(render_entity).contains<History>());
}

TEST_F(TemporalPrepass, WriterCountsAllCameraAndShadowViewsWithDeviceAlignedUniqueOffsets) {
    std::vector<ecs::Entity> views{view(false, 4000), view(true, 4001), view(false, 4002), view(true, 4003)};
    world->entity_mut(views[1]).insert(History{.clip_from_world = glm::mat4{7.0f}});
    prepare();
    wgpu::Limits limits{};
    world->resource<wgpu::Device>().getLimits(&limits);
    const auto alignment = limits.minUniformBufferOffsetAlignment;
    const auto stride = (sizeof(History) + alignment - 1) / alignment * alignment;
    std::vector<std::uint32_t> offsets;
    for (const auto entity : views) {
        ASSERT_TRUE(world->entity(entity).contains<Offset>());
        offsets.push_back(world->entity(entity).get<Offset>()->get().offset);
        EXPECT_EQ(offsets.back() % alignment, 0u);
        EXPECT_EQ(uploaded(entity).clip_from_world, entity == views[1] ? glm::mat4{7.0f} :
            extracted(4000).clip_from_view * glm::inverse(transform(3)));
    }
    std::ranges::sort(offsets);
    for (std::size_t index = 0; index < offsets.size(); ++index) EXPECT_EQ(offsets[index], index * stride);
    EXPECT_GE(world->resource<core::PreviousViewUniforms>().uniforms.buffer()->getSize(), stride * views.size());
}

TEST_F(TemporalPrepass, BufferedZeroViewFrameRetainsTheExistingBuffer) {
    const auto entity = view(false);
    prepare();
    const auto buffer = *world->resource<core::PreviousViewUniforms>().uniforms.buffer();
    world->entity_mut(entity).despawn();
    prepare();
    ASSERT_NE(world->resource<core::PreviousViewUniforms>().uniforms.buffer(), nullptr);
    EXPECT_EQ(*world->resource<core::PreviousViewUniforms>().uniforms.buffer(), buffer);
}

TEST_F(TemporalPrepass, RegisteredExtractionRemovesInactiveHistory) {
    const auto [main_entity, render_entity] = main_camera(false);
    world->entity_mut(render_entity).insert(History{.clip_from_world = glm::mat4{3.0f}});
    extract();
    EXPECT_FALSE(world->entity(render_entity).contains<History>());
}

TEST_F(TemporalPrepass, RegisteredExtractionDoesNotSyncNonCameraHistory) {
    const auto render_entity = world->spawn().id();
    app.world_mut().spawn(render::sync_world::RenderEntity{render_entity}, History{});
    extract();
    EXPECT_FALSE(world->entity(render_entity).contains<History>());
}

TEST_F(TemporalPrepass, PreUpdateSnapshotsCameraAndActiveMeshBeforeUserChanges) {
    const auto [camera_entity, render_entity] = main_camera(true, false);
    const auto mesh = app.world_mut().spawn(epix::mesh::Mesh3d{
        epix::assets::Handle<epix::mesh::Mesh>{epix::assets::AssetId<epix::mesh::Mesh>::invalid()}},
        epix::transform::GlobalTransform{transform(11)}).id();
    ASSERT_TRUE(app.run_schedule(epix::app::PreUpdate));
    ASSERT_TRUE(app.world().entity(camera_entity).contains<History>());
    ASSERT_TRUE(app.world().entity(mesh).contains<pbr::PreviousGlobalTransform>());
    EXPECT_EQ(app.world().entity(mesh).get<pbr::PreviousGlobalTransform>()->get().matrix, transform(11));
    EXPECT_EQ(app.world().entity(camera_entity).get<History>()->get().view_from_world, glm::inverse(transform(7)));
    app.world_mut().entity_mut(mesh).get_mut<epix::transform::GlobalTransform>()->get_mut().matrix = transform(13);
    EXPECT_EQ(app.world().entity(mesh).get<pbr::PreviousGlobalTransform>()->get().matrix, transform(11));
    ASSERT_TRUE(app.run_schedule(epix::app::PreUpdate));
    EXPECT_EQ(app.world().entity(mesh).get<pbr::PreviousGlobalTransform>()->get().matrix, transform(13));
}

TEST_F(TemporalPrepass, InactiveCamerasDoNotEnableMeshHistoryUpdates) {
    main_camera(false, false);
    const auto mesh = app.world_mut().spawn(epix::mesh::Mesh3d{
        epix::assets::Handle<epix::mesh::Mesh>{epix::assets::AssetId<epix::mesh::Mesh>::invalid()}},
        epix::transform::GlobalTransform{transform(11)}).id();
    ASSERT_TRUE(app.run_schedule(epix::app::PreUpdate));
    EXPECT_FALSE(app.world().entity(mesh).contains<pbr::PreviousGlobalTransform>());
}

TEST_F(TemporalPrepass, ExistingMeshHistoryChangesOnlyWhenTransformChanges) {
    const auto [camera_entity, render_entity] = main_camera(true, false);
    const auto mesh = main_mesh(11, true);
    ASSERT_TRUE(app.run_schedule(epix::app::PreUpdate));
    const auto tick = app.world().entity(mesh).get_ref<pbr::PreviousGlobalTransform>()->last_modified();
    EXPECT_EQ(app.world().entity(mesh).get<pbr::PreviousGlobalTransform>()->get().matrix, transform(11));
    ASSERT_TRUE(app.run_schedule(epix::app::PreUpdate));
    EXPECT_EQ(app.world().entity(mesh).get_ref<pbr::PreviousGlobalTransform>()->last_modified(), tick);
    app.world_mut().entity_mut(mesh).get_mut<epix::transform::GlobalTransform>()->get_mut().matrix = transform(13);
    ASSERT_TRUE(app.run_schedule(epix::app::PreUpdate));
    EXPECT_NE(app.world().entity(mesh).get_ref<pbr::PreviousGlobalTransform>()->last_modified(), tick);
    EXPECT_EQ(app.world().entity(mesh).get<pbr::PreviousGlobalTransform>()->get().matrix, transform(13));
    app.world_mut().entity_mut(camera_entity).get_mut<camera::Camera>()->get_mut().is_active = false;
    app.world_mut().entity_mut(mesh).get_mut<epix::transform::GlobalTransform>()->get_mut().matrix = transform(17);
    const auto inactive_tick = app.world().entity(mesh).get_ref<pbr::PreviousGlobalTransform>()->last_modified();
    ASSERT_TRUE(app.run_schedule(epix::app::PreUpdate));
    EXPECT_EQ(app.world().entity(mesh).get<pbr::PreviousGlobalTransform>()->get().matrix, transform(13));
    EXPECT_EQ(app.world().entity(mesh).get_ref<pbr::PreviousGlobalTransform>()->last_modified(), inactive_tick);
}

TEST_F(TemporalPrepass, PreUpdateHistorySurvivesLaterCameraChangesThroughExtractionAndGpuUpload) {
    const auto [main_entity, render_entity] = main_camera(true, false);
    ASSERT_TRUE(app.run_schedule(epix::app::PreUpdate));
    app.world_mut().entity_mut(main_entity).get_mut<epix::transform::GlobalTransform>()->get_mut().matrix = transform(19);
    // This windowless fixture supplies the current extracted view; the registered
    // history extractor and GPU writer must retain the PreUpdate snapshot.
    auto current = extracted(main_entity.index);
    current.world_from_view.matrix = transform(19);
    world->entity_mut(render_entity).insert(current, camera::Camera3d{});
    extract();
    prepare();
    ASSERT_TRUE(world->entity(render_entity).contains<Offset>());
    EXPECT_EQ(uploaded(render_entity).view_from_world, glm::inverse(transform(7)));
    EXPECT_EQ(world->entity(render_entity).get<render::view::ExtractedView>()->get().world_from_view.matrix,
              transform(19));
    ASSERT_TRUE(app.run_schedule(epix::app::PreUpdate));
    extract();
    prepare();
    EXPECT_EQ(uploaded(render_entity).view_from_world, glm::inverse(transform(19)));
}

TEST(TemporalPlugin, SharedMaterialsPluginInstallsMainHistoryOnceWithoutRenderApp) {
    auto app = epix::app::App::create();
    app.add_plugins(pbr::MaterialsPlugin{}, pbr::PrepassPlugin{}, pbr::MaterialsPlugin{});
    unsigned inserts = 0;
    app.world_mut().add_observer([&](ecs::On<ecs::Insert, History>) { ++inserts; });
    const auto camera_entity = app.world_mut().spawn(camera::Camera3d{}, camera::Camera{},
        epix::transform::GlobalTransform{}).id();
    ASSERT_TRUE(app.run_schedule(epix::app::PreUpdate));
    EXPECT_TRUE(app.world().entity(camera_entity).contains<History>());
    EXPECT_EQ(inserts, 1u);
    ASSERT_TRUE(app.run_schedule(epix::app::PreUpdate));
    EXPECT_EQ(inserts, 2u);
}

TEST_F(TemporalPrepass, InactiveCameraStillSnapshotsViewMatricesButUnrelatedCameraAndTransformsDoNot) {
    const auto [camera_entity, render_entity] = main_camera(false, false);
    const auto unrelated_camera = app.world_mut().spawn(camera::Camera{},
        epix::transform::GlobalTransform{transform(11)}).id();
    const auto unrelated_transform = app.world_mut().spawn(epix::transform::GlobalTransform{transform(13)}).id();
    ASSERT_TRUE(app.run_schedule(epix::app::PreUpdate));
    ASSERT_TRUE(app.world().entity(camera_entity).contains<History>());
    const auto data = app.world().entity(camera_entity).get<History>()->get();
    EXPECT_EQ(data.view_from_world, glm::inverse(transform(7)));
    EXPECT_EQ(data.clip_from_world, data.clip_from_view * data.view_from_world);
    EXPECT_EQ(data.world_from_clip, transform(7) * data.view_from_clip);
    EXPECT_EQ(data.view_from_clip, glm::inverse(data.clip_from_view));
    EXPECT_FALSE(app.world().entity(unrelated_camera).contains<History>());
    EXPECT_FALSE(app.world().entity(unrelated_transform).contains<pbr::PreviousGlobalTransform>());
}

TEST_F(TemporalPrepass, ShadowCameraAloneEnablesMainWorldHistoryAndMeshSnapshots) {
    const auto depth = world->resource<wgpu::Device>().createTexture(wgpu::TextureDescriptor{}
        .setSize({8, 8, 1}).setFormat(wgpu::TextureFormat::eDepth32Float)
        .setUsage(wgpu::TextureUsage::eRenderAttachment));
    const auto shadow = app.world_mut().spawn(camera::Camera{}, epix::transform::GlobalTransform{transform(7)},
        pbr::ShadowView{render::render_resource::DepthAttachment{depth.createView(wgpu::TextureViewDescriptor{}), 0.0f},
                        "main_world_shadow"}).id();
    const auto mesh = main_mesh(11);
    ASSERT_TRUE(app.run_schedule(epix::app::PreUpdate));
    ASSERT_TRUE(app.world().entity(shadow).contains<History>());
    EXPECT_EQ(app.world().entity(shadow).get<History>()->get().view_from_world, glm::inverse(transform(7)));
    ASSERT_TRUE(app.world().entity(mesh).contains<pbr::PreviousGlobalTransform>());
    EXPECT_EQ(app.world().entity(mesh).get<pbr::PreviousGlobalTransform>()->get().matrix, transform(11));
}

class TemporalWithoutPbr : public TemporalPrepass {
   protected:
    bool install_pbr() const override { return false; }
};

TEST_F(TemporalWithoutPbr, CoreGraphAloneDoesNotInstallPbrHistoryProduction) {
    const auto [main_entity, render_entity] = main_camera(true, false);
    ASSERT_TRUE(app.run_schedule(epix::app::Update));
    EXPECT_FALSE(app.world().entity(main_entity).contains<History>());
}
}  // namespace
