#include <gtest/gtest.h>

#include <epix/app.hpp>
#include <epix/ecs.hpp>
#include <epix/pbr.hpp>
#include <epix/render/sync_world.hpp>
#include <cmath>
#include <limits>
#include <type_traits>

namespace {
namespace ecs = epix::ecs;
namespace pbr = epix::pbr;
namespace camera = epix::camera;
namespace sync = epix::render::sync_world;

static_assert(std::is_aggregate_v<pbr::SunDisk>);
static_assert(std::is_trivially_copyable_v<pbr::SunDisk>);
static_assert(pbr::SunDisk::EARTH.angular_size == 0.00930842f);
static_assert(pbr::SunDisk::OFF.intensity == 0.0f);

TEST(SunDisk, DefaultsAndNamedConstantsMatchBevy) {
    const pbr::SunDisk defaults{};
    EXPECT_FLOAT_EQ(defaults.angular_size, 0.00930842f);
    EXPECT_FLOAT_EQ(defaults.intensity, 1.0f);
    EXPECT_FLOAT_EQ(pbr::SunDisk::EARTH.angular_size, defaults.angular_size);
    EXPECT_FLOAT_EQ(pbr::SunDisk::EARTH.intensity, defaults.intensity);
    EXPECT_FLOAT_EQ(pbr::SunDisk::OFF.angular_size, 0.0f);
    EXPECT_FLOAT_EQ(pbr::SunDisk::OFF.intensity, 0.0f);
    auto copy = pbr::SunDisk::EARTH;
    copy.intensity = 7.0f;
    EXPECT_FLOAT_EQ(pbr::SunDisk::EARTH.intensity, 1.0f);
}

TEST(SunDisk, RequiresDirectionalLightAndItsTransitiveComponentsWithoutAPlugin) {
    ecs::World world{2};
    const auto entity = world.spawn(pbr::SunDisk::OFF).id();
    EXPECT_TRUE(world.entity(entity).contains<pbr::DirectionalLight>());
    EXPECT_TRUE(world.entity(entity).contains<pbr::Cascades>());
    EXPECT_TRUE(world.entity(entity).contains<pbr::CascadeShadowConfig>());
    EXPECT_TRUE(world.entity(entity).contains<camera::CascadesFrusta>());
    EXPECT_TRUE(world.entity(entity).contains<camera::CascadesVisibleEntities>());
    EXPECT_TRUE(world.entity(entity).contains<epix::transform::Transform>());
    EXPECT_TRUE(world.entity(entity).contains<camera::Visibility>());
    EXPECT_TRUE(world.entity(entity).contains<camera::VisibilityClass>());
    const auto light = world.entity(entity).get<pbr::DirectionalLight>();
    ASSERT_TRUE(light);
    EXPECT_FLOAT_EQ(light->get().illuminance, pbr::light_consts::lux::AMBIENT_DAYLIGHT);
}

TEST(SunDisk, InsertingDiskDoesNotReplaceAnExistingLight) {
    ecs::World world{2};
    const auto entity = world.spawn(pbr::DirectionalLight{.illuminance = 47.0f, .shadows_enabled = true}).id();
    EXPECT_FALSE(world.entity(entity).contains<pbr::SunDisk>());
    world.entity_mut(entity).insert(pbr::SunDisk::OFF);
    const auto light = world.entity(entity).get<pbr::DirectionalLight>();
    ASSERT_TRUE(light);
    EXPECT_FLOAT_EQ(light->get().illuminance, 47.0f);
    EXPECT_TRUE(light->get().shadows_enabled);
}

class SunDiskExtraction : public testing::Test {
   protected:
    ecs::World main_world{2};
    ecs::World render_world{2};
    ecs::Entity light;
    ecs::Entity render_light;

    void SetUp() override {
        render_world.insert_resource(epix::app::ExtractedWorld{main_world});
        main_world.init_resource<pbr::PointLightShadowMap>();
        main_world.init_resource<pbr::DirectionalLightShadowMap>();
        render_light = render_world.spawn().id();
        camera::ViewVisibility visibility;
        visibility.set_visible();
        light = main_world.spawn(pbr::DirectionalLight{}, sync::RenderEntity{render_light},
            epix::transform::GlobalTransform{}, visibility).id();
    }

    void extract() {
        auto system = ecs::make_system_unique(pbr::extract_lights);
        system->initialize(render_world);
        ASSERT_TRUE(system->run({}, render_world).has_value());
        system->apply_deferred(render_world);
    }

    void expect_disk(float angular_size, float intensity) const {
        const auto out = render_world.entity(render_light).get<pbr::ExtractedDirectionalLight>();
        ASSERT_TRUE(out);
        EXPECT_FLOAT_EQ(out->get().sun_disk_angular_size, angular_size);
        EXPECT_FLOAT_EQ(out->get().sun_disk_intensity, intensity);
    }
};

TEST_F(SunDiskExtraction, AbsentComponentUsesEarthNotOff) {
    ASSERT_NO_FATAL_FAILURE(extract());
    // Independent pinned Bevy defaults, not values copied from the extracted light.
    expect_disk(0.00930842f, 1.0f);
    EXPECT_FALSE(main_world.entity(light).contains<pbr::SunDisk>());
}

TEST_F(SunDiskExtraction, ExplicitDefaultUsesEarth) {
    main_world.entity_mut(light).insert(pbr::SunDisk{});
    ASSERT_NO_FATAL_FAILURE(extract());
    expect_disk(0.00930842f, 1.0f);
}

TEST_F(SunDiskExtraction, OptionalParametersAreResolvedIndependentlyForEachLight) {
    const auto other_render_light = render_world.spawn().id();
    camera::ViewVisibility visible;
    visible.set_visible();
    main_world.spawn(pbr::DirectionalLight{}, sync::RenderEntity{other_render_light},
        epix::transform::GlobalTransform{}, visible,
        pbr::SunDisk{.angular_size = 0.05f, .intensity = 6.0f});
    ASSERT_NO_FATAL_FAILURE(extract());
    expect_disk(0.00930842f, 1.0f);
    const auto other = render_world.entity(other_render_light).get<pbr::ExtractedDirectionalLight>();
    ASSERT_TRUE(other);
    EXPECT_FLOAT_EQ(other->get().sun_disk_angular_size, 0.05f);
    EXPECT_FLOAT_EQ(other->get().sun_disk_intensity, 6.0f);
}

TEST_F(SunDiskExtraction, OffHidesDiskWithoutDisablingTheLight) {
    main_world.entity_mut(light).insert(pbr::SunDisk::OFF,
        pbr::DirectionalLight{.illuminance = 71.0f, .shadows_enabled = true});
    ASSERT_NO_FATAL_FAILURE(extract());
    expect_disk(0.0f, 0.0f);
    const auto& out = render_world.entity(render_light).get<pbr::ExtractedDirectionalLight>()->get();
    EXPECT_FLOAT_EQ(out.illuminance, 71.0f);
    EXPECT_TRUE(out.shadows_enabled);
}

TEST_F(SunDiskExtraction, CustomParametersAreCopiedWithoutClampingOrScaling) {
    main_world.entity_mut(light).insert(pbr::SunDisk{.angular_size = 0.0235f, .intensity = 4.25f});
    ASSERT_NO_FATAL_FAILURE(extract());
    expect_disk(0.0235f, 4.25f);
    main_world.entity_mut(light).insert(pbr::SunDisk{.angular_size = -0.5f, .intensity = -2.0f});
    ASSERT_NO_FATAL_FAILURE(extract());
    expect_disk(-0.5f, -2.0f);
}

TEST_F(SunDiskExtraction, NonfiniteParametersAreNotSilentlySanitized) {
    main_world.entity_mut(light).insert(pbr::SunDisk{
        .angular_size = std::numeric_limits<float>::quiet_NaN(),
        .intensity = std::numeric_limits<float>::infinity()});
    ASSERT_NO_FATAL_FAILURE(extract());
    const auto& out = render_world.entity(render_light).get<pbr::ExtractedDirectionalLight>()->get();
    EXPECT_TRUE(std::isnan(out.sun_disk_angular_size));
    EXPECT_EQ(out.sun_disk_intensity, std::numeric_limits<float>::infinity());
}

TEST_F(SunDiskExtraction, ComponentChangesAndRemovalRefreshExtractedParameters) {
    main_world.entity_mut(light).insert(pbr::SunDisk::OFF);
    ASSERT_NO_FATAL_FAILURE(extract());
    expect_disk(0.0f, 0.0f);
    main_world.entity_mut(light).insert(pbr::SunDisk{.angular_size = 0.04f, .intensity = 2.5f});
    ASSERT_NO_FATAL_FAILURE(extract());
    expect_disk(0.04f, 2.5f);
    main_world.entity_mut(light).remove<pbr::SunDisk>();
    ASSERT_NO_FATAL_FAILURE(extract());
    expect_disk(0.00930842f, 1.0f);
}

TEST_F(SunDiskExtraction, VisibilityLossRemovesTheLightAndReappearanceUsesCurrentDisk) {
    main_world.entity_mut(light).insert(pbr::SunDisk::OFF);
    ASSERT_NO_FATAL_FAILURE(extract());
    main_world.entity_mut(light).insert(camera::ViewVisibility::HIDDEN);
    ASSERT_NO_FATAL_FAILURE(extract());
    EXPECT_FALSE(render_world.entity(render_light).contains<pbr::ExtractedDirectionalLight>());
    camera::ViewVisibility visible;
    visible.set_visible();
    main_world.entity_mut(light).insert(visible, pbr::SunDisk{.angular_size = 0.03f, .intensity = 3.0f});
    ASSERT_NO_FATAL_FAILURE(extract());
    expect_disk(0.03f, 3.0f);
}
}  // namespace
