#include <gtest/gtest.h>

#include <epix/app.hpp>
#include <epix/ecs.hpp>
#include <epix/pbr.hpp>
#include <epix/render/sync_world.hpp>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

namespace {
namespace ecs = epix::ecs;
namespace pbr = epix::pbr;
namespace camera = epix::camera;
namespace sync = epix::render::sync_world;

struct LightPair { ecs::Entity main; ecs::Entity render; };
struct MapTicks { ecs::Tick point; ecs::Tick directional; };

class LightExtraction : public testing::Test {
   protected:
    ecs::World main_world{2};
    ecs::World render_world{2};
    decltype(ecs::make_system_unique(pbr::extract_lights)) system;
    ecs::Entity mesh0, mesh1, missing_mesh, render_mesh0, render_mesh1;

    void SetUp() override {
        render_world.insert_resource(epix::app::ExtractedWorld{main_world});
        main_world.insert_resource(pbr::PointLightShadowMap{.size = 512});
        main_world.insert_resource(pbr::DirectionalLightShadowMap{.size = 2048});
        main_world.init_resource<pbr::GlobalVisibleClusterableObjects>();
        for (int index = 0; index < 32; ++index) render_world.spawn();
        render_mesh0 = render_world.spawn().id();
        render_mesh1 = render_world.spawn().id();
        mesh0 = main_world.spawn(sync::RenderEntity{render_mesh0}).id();
        mesh1 = main_world.spawn(sync::RenderEntity{render_mesh1}).id();
        missing_mesh = main_world.spawn().id();
    }

    static epix::transform::GlobalTransform transform() {
        glm::mat4 matrix{1.0f};
        matrix[0] = {2, 0.2f, 0.3f, 0};
        matrix[1] = {0.4f, 3, 0.5f, 0};
        matrix[2] = {0.6f, 0.7f, 4, 0};
        matrix[3] = {5, 6, 7, 1};
        return {matrix};
    }

    static camera::ViewVisibility visibility(bool visible) {
        camera::ViewVisibility result;
        if (visible) result.set_visible();
        return result;
    }

    void enlist(ecs::Entity entity) {
        main_world.resource_mut<pbr::GlobalVisibleClusterableObjects>().entities.insert(entity);
    }

    LightPair point(bool listed = true, bool visible = true, bool volumetric = false) {
        const auto render = render_world.spawn().id();
        pbr::PointLight light{
            .color = epix::color::Color::srgba(0.5f, 0.25f, 0.75f, 0.8f),
            .intensity = 80.0f * std::numbers::pi_v<float>, .range = 17.0f, .radius = 0.3f,
            .shadows_enabled = true, .affects_lightmapped_mesh_diffuse = false,
            .shadow_depth_bias = 0.13f, .shadow_normal_bias = 0.7f, .shadow_map_near_z = 0.2f};
        camera::CubemapVisibleEntities visible_meshes;
        camera::CubemapFrusta frusta;
        for (std::size_t face = 0; face < 6; ++face) {
            auto& list = visible_meshes.get_mut(face).entities;
            list = {mesh0, missing_mesh, mesh1};
            list.insert(list.end(), face, mesh0);
            frusta.frusta[face].planes[0] = camera::HalfSpace::from_normal_d({1, 0, 0, float(face + 1)});
        }
        const auto main = main_world.spawn(light, sync::RenderEntity{render}, transform(), visibility(visible),
            std::move(visible_meshes), std::move(frusta)).id();
        if (volumetric) main_world.entity_mut(main).insert(pbr::VolumetricLight{});
        if (listed) enlist(main);
        EXPECT_NE(main, render);
        return {main, render};
    }

    LightPair spot(bool listed = true, bool visible = true, bool volumetric = false) {
        const auto render = render_world.spawn().id();
        pbr::SpotLight light{
            .color = epix::color::Color::srgba(0.5f, 0.25f, 0.75f, 0.8f),
            .intensity = 120.0f * std::numbers::pi_v<float>, .range = 19.0f, .radius = 0.4f,
            .shadows_enabled = true, .affects_lightmapped_mesh_diffuse = false,
            .shadow_depth_bias = 0.17f, .shadow_normal_bias = 1.3f, .shadow_map_near_z = 0.25f,
            .outer_angle = 0.6f, .inner_angle = 0.2f};
        camera::Frustum frustum;
        frustum.planes[1] = camera::HalfSpace::from_normal_d({0, 1, 0, 7});
        const auto main = main_world.spawn(light, sync::RenderEntity{render}, transform(), visibility(visible),
            camera::VisibleMeshEntities{.entities = {mesh0, missing_mesh, mesh1}}, frustum).id();
        if (volumetric) main_world.entity_mut(main).insert(pbr::VolumetricLight{});
        if (listed) enlist(main);
        EXPECT_NE(main, render);
        return {main, render};
    }

    LightPair directional(bool volumetric = false) {
        const auto render = render_world.spawn().id();
        const auto main = main_world.spawn(pbr::DirectionalLight{}, sync::RenderEntity{render},
            transform(), visibility(true)).id();
        if (volumetric) main_world.entity_mut(main).insert(pbr::VolumetricLight{});
        return {main, render};
    }

    void extract() {
        if (!system) {
            system = ecs::make_system_unique(pbr::extract_lights);
            system->initialize(render_world);
        }
        ASSERT_TRUE(system->run({}, render_world).has_value());
        system->apply_deferred(render_world);
    }

    void observe_ticks() {
        render_world.init_resource<MapTicks>();
        auto observer = ecs::make_system_unique([](ecs::Res<pbr::PointLightShadowMap> point,
            ecs::Res<pbr::DirectionalLightShadowMap> directional, ecs::ResMut<MapTicks> result) {
            result->point = point.last_modified();
            result->directional = directional.last_modified();
        });
        observer->initialize(render_world);
        ASSERT_TRUE(observer->run({}, render_world).has_value());
    }

    void expect_visible(const pbr::RenderVisibleMeshEntities& list, std::size_t extra = 0) const {
        std::vector<std::pair<ecs::Entity, sync::MainEntity>> expected{
            {render_mesh0, sync::MainEntity{mesh0}},
            {ecs::Entity::PLACEHOLDER, sync::MainEntity{missing_mesh}},
            {render_mesh1, sync::MainEntity{mesh1}}};
        expected.insert(expected.end(), extra, {render_mesh0, sync::MainEntity{mesh0}});
        EXPECT_EQ(list.entities, expected);
    }
};

TEST_F(LightExtraction, PointCopiesAllFieldsFacesFrustaAndMainIdentity) {
    const auto light = point(true, true, true);
    ASSERT_NO_FATAL_FAILURE(extract());
    const auto out = render_world.entity(light.render).get<pbr::ExtractedPointLight>();
    ASSERT_TRUE(out);
    const auto& value = out->get();
    EXPECT_EQ(value.color, epix::color::Color::srgba(0.5f, 0.25f, 0.75f, 0.8f).linear().vec4());
    EXPECT_FLOAT_EQ(value.intensity, 20.0f);
    EXPECT_FLOAT_EQ(value.range, 17.0f);
    EXPECT_FLOAT_EQ(value.radius, 0.3f);
    EXPECT_EQ(value.transform, transform().matrix);
    EXPECT_TRUE(value.shadows_enabled);
    EXPECT_FALSE(value.affects_lightmapped_mesh_diffuse);
    EXPECT_TRUE(value.volumetric);
    EXPECT_FALSE(value.soft_shadows_enabled);
    EXPECT_FALSE(value.spot_light_angles);
    EXPECT_FLOAT_EQ(value.shadow_depth_bias, 0.13f);
    EXPECT_FLOAT_EQ(value.shadow_normal_bias, 0.7f * (2.0f / 512.0f) * std::numbers::sqrt2_v<float>);
    EXPECT_FLOAT_EQ(value.shadow_map_near_z, 0.2f);
    const auto faces = render_world.entity(light.render).get<pbr::RenderCubemapVisibleEntities>();
    const auto frusta = render_world.entity(light.render).get<camera::CubemapFrusta>();
    ASSERT_TRUE(faces);
    ASSERT_TRUE(frusta);
    for (std::size_t face = 0; face < 6; ++face) {
        expect_visible(faces->get().get(face), face);
        EXPECT_EQ(frusta->get().frusta[face].planes,
            main_world.entity(light.main).get<camera::CubemapFrusta>()->get().frusta[face].planes);
    }
    const auto main = render_world.entity(light.render).get<sync::MainEntity>();
    ASSERT_TRUE(main);
    EXPECT_EQ(main->get().id(), light.main);
}

TEST_F(LightExtraction, SpotUsesFourPiAndDirectionalShadowResolution) {
    const auto light = spot(true, true, true);
    ASSERT_NO_FATAL_FAILURE(extract());
    const auto out = render_world.entity(light.render).get<pbr::ExtractedPointLight>();
    ASSERT_TRUE(out);
    const auto& value = out->get();
    EXPECT_EQ(value.color, epix::color::Color::srgba(0.5f, 0.25f, 0.75f, 0.8f).linear().vec4());
    EXPECT_FLOAT_EQ(value.intensity, 30.0f);
    EXPECT_FLOAT_EQ(value.range, 19.0f);
    EXPECT_FLOAT_EQ(value.radius, 0.4f);
    EXPECT_EQ(value.transform, transform().matrix);
    EXPECT_TRUE(value.shadows_enabled);
    EXPECT_FALSE(value.affects_lightmapped_mesh_diffuse);
    EXPECT_TRUE(value.volumetric);
    EXPECT_FALSE(value.soft_shadows_enabled);
    ASSERT_TRUE(value.spot_light_angles);
    EXPECT_EQ(*value.spot_light_angles, (std::pair{0.2f, 0.6f}));
    EXPECT_FLOAT_EQ(value.shadow_depth_bias, 0.17f);
    EXPECT_FLOAT_EQ(value.shadow_normal_bias,
        1.3f * (2.0f * std::tan(0.6f) / 2048.0f) * std::numbers::sqrt2_v<float>);
    EXPECT_FLOAT_EQ(value.shadow_map_near_z, 0.25f);
    const auto list = render_world.entity(light.render).get<pbr::RenderVisibleMeshEntities>();
    const auto frustum = render_world.entity(light.render).get<camera::Frustum>();
    ASSERT_TRUE(list);
    ASSERT_TRUE(frustum);
    expect_visible(list->get());
    EXPECT_EQ(frustum->get().planes, main_world.entity(light.main).get<camera::Frustum>()->get().planes);
    const auto main = render_world.entity(light.render).get<sync::MainEntity>();
    ASSERT_TRUE(main);
    EXPECT_EQ(main->get().id(), light.main);
}

TEST_F(LightExtraction, GlobalMembershipAndViewVisibilityAreBothRequired) {
    const auto unlisted_point = point(false);
    const auto unlisted_spot = spot(false);
    const auto hidden_point = point(true, false);
    const auto hidden_spot = spot(true, false);
    ASSERT_NO_FATAL_FAILURE(extract());
    for (auto light : {unlisted_point, unlisted_spot, hidden_point, hidden_spot})
        EXPECT_FALSE(render_world.entity(light.render).contains<pbr::ExtractedPointLight>());
}

TEST_F(LightExtraction, MissingRequiredSourceComponentsAreSkipped) {
    const auto a = point();
    const auto b = spot();
    main_world.entity_mut(a.main).remove<camera::CubemapFrusta>();
    main_world.entity_mut(b.main).remove<camera::Frustum>();
    enlist(main_world.spawn().id());
    const auto stale_main = main_world.spawn().id();
    main_world.entity_mut(stale_main).despawn();
    enlist(stale_main);
    ASSERT_NO_FATAL_FAILURE(extract());
    EXPECT_FALSE(render_world.entity(a.render).contains<pbr::ExtractedPointLight>());
    EXPECT_FALSE(render_world.entity(b.render).contains<pbr::ExtractedPointLight>());
}

TEST_F(LightExtraction, PreviousListsClearWithoutDeletingUnlistedLightRecords) {
    pbr::RenderCubemapVisibleEntities faces;
    for (auto& face : faces.iter_mut()) face.entities.emplace_back(render_mesh0, sync::MainEntity{mesh0});
    const auto a = render_world.spawn(pbr::ExtractedPointLight{.intensity = 37.0f}, std::move(faces)).id();
    const auto b = render_world.spawn(pbr::ExtractedPointLight{.intensity = 41.0f},
        pbr::RenderVisibleMeshEntities{.entities = {{render_mesh1, sync::MainEntity{mesh1}}}}).id();
    // An unrelated visibility component without an extracted light is not cleared.
    const auto unrelated = render_world.spawn(pbr::RenderVisibleMeshEntities{
        .entities = {{render_mesh0, sync::MainEntity{mesh0}}}}).id();
    ASSERT_NO_FATAL_FAILURE(extract());
    for (const auto& face : render_world.entity(a).get<pbr::RenderCubemapVisibleEntities>()->get().iter())
        EXPECT_TRUE(face.entities.empty());
    EXPECT_TRUE(render_world.entity(b).get<pbr::RenderVisibleMeshEntities>()->get().entities.empty());
    EXPECT_FLOAT_EQ(render_world.entity(a).get<pbr::ExtractedPointLight>()->get().intensity, 37.0f);
    EXPECT_FLOAT_EQ(render_world.entity(b).get<pbr::ExtractedPointLight>()->get().intensity, 41.0f);
    EXPECT_EQ(render_world.entity(unrelated).get<pbr::RenderVisibleMeshEntities>()->get().entities.size(), 1u);
}

TEST_F(LightExtraction, VisibilityLossClearsListsButRetainsPreviousPointAndSpotValues) {
    const auto a = point();
    const auto b = spot();
    ASSERT_NO_FATAL_FAILURE(extract());
    ASSERT_TRUE(render_world.entity(a.render).contains<pbr::ExtractedPointLight>());
    ASSERT_TRUE(render_world.entity(b.render).contains<pbr::ExtractedPointLight>());
    main_world.resource_mut<pbr::GlobalVisibleClusterableObjects>().entities.erase(a.main);
    main_world.entity_mut(b.main).insert(camera::ViewVisibility::HIDDEN);
    ASSERT_NO_FATAL_FAILURE(extract());
    for (const auto& face : render_world.entity(a.render).get<pbr::RenderCubemapVisibleEntities>()->get().iter())
        EXPECT_TRUE(face.entities.empty());
    EXPECT_TRUE(render_world.entity(b.render).get<pbr::RenderVisibleMeshEntities>()->get().entities.empty());
    EXPECT_TRUE(render_world.entity(a.render).contains<pbr::ExtractedPointLight>());
    EXPECT_TRUE(render_world.entity(b.render).contains<pbr::ExtractedPointLight>());
}

TEST_F(LightExtraction, SubsequentExtractionRefreshesValuesListsAndVolumetricPresence) {
    const auto a = point();
    const auto b = spot();
    ASSERT_NO_FATAL_FAILURE(extract());
    ASSERT_TRUE(render_world.entity(a.render).contains<pbr::ExtractedPointLight>());
    EXPECT_FALSE(render_world.entity(a.render).get<pbr::ExtractedPointLight>()->get().volumetric);
    EXPECT_FALSE(render_world.entity(b.render).get<pbr::ExtractedPointLight>()->get().volumetric);
    main_world.entity_mut(a.main).get_mut<pbr::PointLight>()->get_mut().intensity = 16.0f * std::numbers::pi_v<float>;
    main_world.entity_mut(a.main).insert(pbr::VolumetricLight{});
    main_world.entity_mut(b.main).insert(pbr::VolumetricLight{});
    main_world.entity_mut(b.main).get_mut<camera::VisibleMeshEntities>()->get_mut().entities = {mesh1};
    ASSERT_NO_FATAL_FAILURE(extract());
    EXPECT_FLOAT_EQ(render_world.entity(a.render).get<pbr::ExtractedPointLight>()->get().intensity, 4.0f);
    EXPECT_TRUE(render_world.entity(a.render).get<pbr::ExtractedPointLight>()->get().volumetric);
    EXPECT_TRUE(render_world.entity(b.render).get<pbr::ExtractedPointLight>()->get().volumetric);
    const auto& result = render_world.entity(b.render).get<pbr::RenderVisibleMeshEntities>()->get().entities;
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], (std::pair{render_mesh1, sync::MainEntity{mesh1}}));
    main_world.entity_mut(a.main).remove<pbr::VolumetricLight>();
    main_world.entity_mut(b.main).remove<pbr::VolumetricLight>();
    ASSERT_NO_FATAL_FAILURE(extract());
    EXPECT_FALSE(render_world.entity(a.render).get<pbr::ExtractedPointLight>()->get().volumetric);
    EXPECT_FALSE(render_world.entity(b.render).get<pbr::ExtractedPointLight>()->get().volumetric);
}

TEST_F(LightExtraction, StaleRenderTargetsAreNotRespawnedOrAllowedToAbortTheGather) {
    const auto stale_point = point();
    const auto stale_spot = spot();
    const auto good_point = point();
    const auto good_spot = spot();
    render_world.entity_mut(stale_point.render).despawn();
    render_world.entity_mut(stale_spot.render).despawn();
    ASSERT_NO_FATAL_FAILURE(extract());
    EXPECT_FALSE(render_world.get_entity(stale_point.render));
    EXPECT_FALSE(render_world.get_entity(stale_spot.render));
    EXPECT_TRUE(render_world.entity(good_point.render).contains<pbr::ExtractedPointLight>());
    EXPECT_TRUE(render_world.entity(good_spot.render).contains<pbr::ExtractedPointLight>());
}

TEST_F(LightExtraction, DeferredRecordsOwnSourceValuesAndFrusta) {
    const auto a = point();
    const auto b = spot();
    system = ecs::make_system_unique(pbr::extract_lights);
    system->initialize(render_world);
    ASSERT_TRUE(system->run({}, render_world).has_value());
    // The producer has returned, but insertion has not run yet. Neither source
    // edits nor destruction of its loop-local records may change the queued copy.
    main_world.entity_mut(a.main).get_mut<pbr::PointLight>()->get_mut().intensity = 0;
    main_world.entity_mut(a.main).get_mut<camera::CubemapFrusta>()->get_mut().frusta[0].planes[0] =
        camera::HalfSpace::from_normal_d({0, 1, 0, 99});
    main_world.entity_mut(b.main).get_mut<camera::VisibleMeshEntities>()->get_mut().entities.clear();
    system->apply_deferred(render_world);
    const auto out = render_world.entity(a.render).get<pbr::ExtractedPointLight>();
    ASSERT_TRUE(out);
    EXPECT_FLOAT_EQ(out->get().intensity, 20.0f);
    EXPECT_FLOAT_EQ(render_world.entity(a.render).get<camera::CubemapFrusta>()->get().frusta[0].planes[0].d(), 1.0f);
    const auto visible = render_world.entity(b.render).get<pbr::RenderVisibleMeshEntities>();
    ASSERT_TRUE(visible);
    expect_visible(visible->get());
}

TEST_F(LightExtraction, SpotArmRunsAfterPointArmForAMixedSourceEntity) {
    const auto light = point();
    main_world.entity_mut(light.main).insert(pbr::SpotLight{.intensity = 28.0f * std::numbers::pi_v<float>},
        camera::Frustum{}, camera::VisibleMeshEntities{.entities = {mesh1}});
    ASSERT_NO_FATAL_FAILURE(extract());
    const auto out = render_world.entity(light.render).get<pbr::ExtractedPointLight>();
    ASSERT_TRUE(out);
    EXPECT_FLOAT_EQ(out->get().intensity, 7.0f);
    EXPECT_TRUE(out->get().spot_light_angles);
    EXPECT_TRUE(render_world.entity(light.render).contains<pbr::RenderCubemapVisibleEntities>());
    EXPECT_TRUE(render_world.entity(light.render).contains<pbr::RenderVisibleMeshEntities>());
}

TEST_F(LightExtraction, DirectionalVolumetricMarkerIsOptionalAndRefreshes) {
    const auto light = directional(true);
    ASSERT_NO_FATAL_FAILURE(extract());
    const auto out = render_world.entity(light.render).get<pbr::ExtractedDirectionalLight>();
    ASSERT_TRUE(out);
    EXPECT_TRUE(out->get().volumetric);
    main_world.entity_mut(light.main).remove<pbr::VolumetricLight>();
    ASSERT_NO_FATAL_FAILURE(extract());
    EXPECT_FALSE(render_world.entity(light.render).get<pbr::ExtractedDirectionalLight>()->get().volumetric);
}

TEST_F(LightExtraction, DirectionalQueryExcludesSourcesWithSpotLight) {
    const auto light = directional();
    main_world.entity_mut(light.main).insert(pbr::SpotLight{}, camera::VisibleMeshEntities{}, camera::Frustum{});
    enlist(light.main);
    ASSERT_NO_FATAL_FAILURE(extract());
    EXPECT_FALSE(render_world.entity(light.render).contains<pbr::ExtractedDirectionalLight>());
    EXPECT_FALSE(render_world.entity(light.render).contains<pbr::RenderCascadesVisibleEntities>());
    EXPECT_TRUE(render_world.entity(light.render).contains<pbr::ExtractedPointLight>());
}

TEST_F(LightExtraction, DirectionalDoesNotDependOnGlobalClusterableMembership) {
    const auto light = directional();
    ASSERT_NO_FATAL_FAILURE(extract());
    EXPECT_TRUE(render_world.entity(light.render).contains<pbr::ExtractedDirectionalLight>());
}

TEST_F(LightExtraction, UnchangedShadowResourcesKeepTheirRenderModificationTicks) {
    ASSERT_NO_FATAL_FAILURE(extract());
    ASSERT_NO_FATAL_FAILURE(observe_ticks());
    const auto before = render_world.resource<MapTicks>();
    ASSERT_NO_FATAL_FAILURE(extract());
    ASSERT_NO_FATAL_FAILURE(observe_ticks());
    const auto after = render_world.resource<MapTicks>();
    EXPECT_EQ(after.point, before.point);
    EXPECT_EQ(after.directional, before.directional);
    EXPECT_EQ(render_world.resource<pbr::PointLightShadowMap>().size, 512u);
    EXPECT_EQ(render_world.resource<pbr::DirectionalLightShadowMap>().size, 2048u);
}

TEST_F(LightExtraction, ShadowResourceChangesCopyIndependently) {
    ASSERT_NO_FATAL_FAILURE(extract());
    ASSERT_NO_FATAL_FAILURE(observe_ticks());
    const auto first = render_world.resource<MapTicks>();
    main_world.resource_mut<pbr::PointLightShadowMap>().size = 128;
    ASSERT_NO_FATAL_FAILURE(extract());
    ASSERT_NO_FATAL_FAILURE(observe_ticks());
    const auto second = render_world.resource<MapTicks>();
    EXPECT_NE(second.point, first.point);
    EXPECT_EQ(second.directional, first.directional);
    EXPECT_EQ(render_world.resource<pbr::PointLightShadowMap>().size, 128u);
    main_world.resource_mut<pbr::DirectionalLightShadowMap>().size = 1024;
    ASSERT_NO_FATAL_FAILURE(extract());
    ASSERT_NO_FATAL_FAILURE(observe_ticks());
    const auto third = render_world.resource<MapTicks>();
    EXPECT_EQ(third.point, second.point);
    EXPECT_NE(third.directional, second.directional);
    EXPECT_EQ(render_world.resource<pbr::DirectionalLightShadowMap>().size, 1024u);
}

TEST_F(LightExtraction, EqualValueSourceMutationStillPropagatesItsChangeTick) {
    ASSERT_NO_FATAL_FAILURE(extract());
    ASSERT_NO_FATAL_FAILURE(observe_ticks());
    const auto first = render_world.resource<MapTicks>();
    main_world.resource_mut<pbr::PointLightShadowMap>().size = 512;
    ASSERT_NO_FATAL_FAILURE(extract());
    ASSERT_NO_FATAL_FAILURE(observe_ticks());
    const auto second = render_world.resource<MapTicks>();
    EXPECT_NE(second.point, first.point);
    EXPECT_EQ(second.directional, first.directional);
    ASSERT_NO_FATAL_FAILURE(extract());
    ASSERT_NO_FATAL_FAILURE(observe_ticks());
    const auto third = render_world.resource<MapTicks>();
    EXPECT_EQ(third.point, second.point);
    EXPECT_EQ(third.directional, second.directional);
}
}  // namespace
