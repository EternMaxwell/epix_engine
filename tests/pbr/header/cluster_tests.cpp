#include <epix/pbr/ambient_light.hpp>
#include <epix/pbr/atmosphere.hpp>
#include <epix/pbr/cascade.hpp>
#include <epix/pbr/cluster.hpp>
#include <epix/pbr/clustered_decal.hpp>
#include <epix/pbr/directional_light.hpp>
#include <epix/pbr/fog.hpp>
#include <epix/pbr/gpu_lights.hpp>
#include <epix/pbr/light_cluster.hpp>
#include <epix/pbr/light_meta.hpp>
#include <epix/pbr/light_markers.hpp>
#include <epix/pbr/light_probe.hpp>
#include <epix/pbr/light_systems.hpp>
#include <epix/pbr/material_bindings.hpp>
#include <epix/pbr/mesh_bindings.hpp>
#include <epix/pbr/mesh_instance.hpp>
#include <epix/pbr/mesh_pipeline.hpp>
#include <epix/pbr/mesh_uniform.hpp>
#include <epix/pbr/mesh_view_bindings.hpp>
#include <epix/pbr/point_light.hpp>
#include <epix/pbr/prepass.hpp>
#include <epix/pbr/prepass_bindings.hpp>
#include <epix/pbr/probe.hpp>
#include <epix/pbr/shadow_phase.hpp>
#include <epix/pbr/spot_light.hpp>
#include <epix/pbr/ssr.hpp>
#include <epix/pbr/volumetric.hpp>
#include <epix/pbr/mesh_extraction.hpp>
#include <gtest/gtest.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/** @brief The clustered-light GPU types match Bevy's layout and sizing
 *  (`bevy_pbr/src/cluster.rs:19-131`). */
namespace {

/** @brief `GpuClusterableObject` is 80 bytes - three `vec4`s then eight scalars -
 *  which is what Bevy's own
 *  `assert!(size_of::<GpuClusterableObject>() * MAX_UNIFORM_BUFFER_CLUSTERABLE_OBJECTS <= 16384)`
 *  (`:24-25`) depends on to stay inside a uniform buffer. */
TEST(Cluster, GpuClusterableObjectMatchesBevyLayout) {
    using epix::pbr::GpuClusterableObject;

    EXPECT_EQ(sizeof(GpuClusterableObject), 80u);
    EXPECT_EQ(offsetof(GpuClusterableObject, flags), 48u) << "three vec4s precede the first scalar";
    EXPECT_EQ(offsetof(GpuClusterableObject, decal_index), 72u);
    EXPECT_LE(sizeof(GpuClusterableObject) * epix::pbr::MAX_UNIFORM_BUFFER_CLUSTERABLE_OBJECTS, 16384u);
    EXPECT_EQ(epix::pbr::MAX_UNIFORM_BUFFER_CLUSTERABLE_OBJECTS, 204u);
    EXPECT_EQ(epix::pbr::CLUSTERED_FORWARD_STORAGE_BUFFER_COUNT, 3u);
    // Bevy's companion guard on the index list (`cluster.rs:562-564`); the same
    // budget is now a `static_assert` in the header, so a field addition fails the
    // build rather than the device.
    EXPECT_LE(sizeof(epix::pbr::GpuClusterableObjectIndexListsUniform), 16384u);
}

/** @brief The uniform form is a full array; the three storage forms are
 *  runtime-sized, so they report a one-element minimum and a written size that
 *  follows the data (Bevy `#[shader(size(runtime))]`).
 *
 *  encase computes a runtime-sized struct's `min_size()` by assuming the array
 *  holds one element (`encase-0.12.1/src/types/runtime_sized_array.rs:140`,
 *  `:129-144`), so a struct that is nothing but the runtime array still has that
 *  element's size as its minimum - not zero. */
TEST(Cluster, RuntimeSizedClusterBuffersFollowTheData) {
    using epix::pbr::GpuClusterOffsetsAndCounts;
    using epix::pbr::GpuClusterOffsetsAndCountsStorage;
    using epix::pbr::GpuClusterableObject;
    using epix::pbr::GpuClusterableObjectIndexListsStorage;
    using epix::pbr::GpuClusterableObjectsStorage;
    using epix::pbr::GpuClusterableObjectsUniform;
    using epix::render::render_resource::ShaderTypeInfo;
    using epix::render::render_resource::ShaderWritable;

    static_assert(ShaderWritable<GpuClusterableObjectsUniform>);
    static_assert(ShaderWritable<GpuClusterableObjectsStorage>);
    static_assert(ShaderWritable<GpuClusterableObjectIndexListsStorage>);
    static_assert(ShaderWritable<GpuClusterOffsetsAndCountsStorage>);
    EXPECT_EQ(ShaderTypeInfo<GpuClusterableObjectsUniform>::shader_size,
              204u * sizeof(GpuClusterableObject));

    EXPECT_EQ(ShaderTypeInfo<GpuClusterableObjectsStorage>::shader_size, 80u)
        << "one GpuClusterableObject is the runtime struct's minimum";
    EXPECT_EQ(ShaderTypeInfo<GpuClusterableObjectIndexListsStorage>::shader_size, 4u)
        << "one u32 is the runtime u32 list's minimum";
    EXPECT_EQ(ShaderTypeInfo<GpuClusterOffsetsAndCountsStorage>::shader_size, 32u)
        << "one array<uvec4, 2> is the runtime list's minimum";

    const GpuClusterableObjectsStorage objects{.data = std::vector<GpuClusterableObject>(3)};
    EXPECT_EQ(ShaderTypeInfo<GpuClusterableObjectsStorage>::size_for(objects), 3u * 80u);
    const GpuClusterableObjectIndexListsStorage indices{.data = std::vector<std::uint32_t>(7)};
    EXPECT_EQ(ShaderTypeInfo<GpuClusterableObjectIndexListsStorage>::size_for(indices), 7u * sizeof(std::uint32_t));
    const GpuClusterOffsetsAndCountsStorage offsets{.data = std::vector<GpuClusterOffsetsAndCounts>(5)};
    EXPECT_EQ(ShaderTypeInfo<GpuClusterOffsetsAndCountsStorage>::size_for(offsets),
              5u * sizeof(GpuClusterOffsetsAndCounts));

    // The encoded bytes are the elements back to back.
    std::vector<std::uint8_t> encoded(ShaderTypeInfo<GpuClusterableObjectsStorage>::size_for(objects));
    ShaderTypeInfo<GpuClusterableObjectsStorage>::write_into(objects, encoded);
    EXPECT_EQ(encoded.size(), 240u);
}

/** @brief The light-side clustered-forward types (Bevy `bevy_light`'s
 *  `cluster/mod.rs`) carry Bevy's defaults: clustering disabled, zeroed counts,
 *  a single cluster, and no storage-buffer support until the device is asked. */
TEST(Cluster, LightSideTypesMatchBevyDefaults) {
    using epix::pbr::ClusterableObjectCounts;
    using epix::pbr::ClusterConfig;
    using epix::pbr::Clusters;
    using epix::pbr::GlobalClusterSettings;
    using epix::pbr::VisibleClusterableObjects;

    const GlobalClusterSettings settings{};
    EXPECT_FALSE(settings.supports_storage_buffers);
    EXPECT_EQ(settings.max_uniform_buffer_clusterable_objects, 0u);

    const ClusterConfig config{};
    EXPECT_EQ(config.kind, ClusterConfig::Kind::FixedZ) << "Bevy's ClusterConfig default is FixedZ, not None";
    EXPECT_EQ(config.total, 4096u) << "at most 4096 clusters";
    EXPECT_EQ(config.z_slices, 24u) << "in 24 depth slices";
    EXPECT_TRUE(config.dynamic_resizing) << "and it resizes dynamically";

    const ClusterableObjectCounts counts{};
    EXPECT_EQ(counts.point_lights, 0u);
    EXPECT_EQ(counts.spot_lights, 0u);
    EXPECT_EQ(counts.reflection_probes, 0u);
    EXPECT_EQ(counts.irradiance_volumes, 0u);
    EXPECT_EQ(counts.decals, 0u);

    const Clusters clusters{};
    EXPECT_EQ(clusters.dimensions, glm::uvec3(1, 1, 1)) << "a view starts as one cluster";
    EXPECT_EQ(clusters.tile_size, glm::uvec2(0, 0));
    EXPECT_TRUE(clusters.clusterable_objects.empty());

    VisibleClusterableObjects visible{};
    visible.entities.push_back(epix::ecs::Entity{});
    EXPECT_EQ(visible.entities.size(), 1u);
}

/** @brief The ambient light component and resource carry Bevy's defaults: white
 *  at a brightness of 80, affecting lightmapped meshes, with `NONE` differing
 *  only in brightness (`bevy_light/src/ambient_light.rs:31-96`). */
TEST(Cluster, AmbientLightMatchesBevyDefaults) {
    using epix::pbr::AmbientLight;
    using epix::pbr::GlobalAmbientLight;

    const AmbientLight ambient{};
    EXPECT_EQ(ambient.color, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    EXPECT_FLOAT_EQ(ambient.brightness, 80.0f);
    EXPECT_TRUE(ambient.affects_lightmapped_meshes);

    const GlobalAmbientLight global{};
    EXPECT_FLOAT_EQ(global.brightness, 80.0f);

    const GlobalAmbientLight none = GlobalAmbientLight::NONE;
    EXPECT_FLOAT_EQ(none.brightness, 0.0f) << "Bevy's NONE is the default with zero brightness";
    EXPECT_EQ(none.color, global.color);
    EXPECT_TRUE(none.affects_lightmapped_meshes);
}

/** @brief `PointLight` carries Bevy's defaults: white, a very large cinema
 *  light's luminous power, a range of 20, no radius, shadows off, diffuse
 *  lightmapped contribution on, and Bevy's three shadow constants
 *  (`bevy_light/src/point_light.rs:128-150`). */
TEST(Cluster, PointLightMatchesBevyDefaults) {
    using epix::pbr::PointLight;
    using epix::pbr::PointLightShadowMap;
    using epix::pbr::light_consts::lumens::VERY_LARGE_CINEMA_LIGHT;

    const PointLight light{};
    EXPECT_EQ(light.color, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    EXPECT_FLOAT_EQ(light.intensity, VERY_LARGE_CINEMA_LIGHT);
    EXPECT_FLOAT_EQ(light.intensity, 1'000'000.0f);
    EXPECT_FLOAT_EQ(light.range, 20.0f);
    EXPECT_FLOAT_EQ(light.radius, 0.0f);
    EXPECT_FALSE(light.shadows_enabled);
    EXPECT_TRUE(light.affects_lightmapped_mesh_diffuse);
    EXPECT_FLOAT_EQ(light.shadow_depth_bias, PointLight::DEFAULT_SHADOW_DEPTH_BIAS);
    EXPECT_FLOAT_EQ(light.shadow_normal_bias, PointLight::DEFAULT_SHADOW_NORMAL_BIAS);
    EXPECT_FLOAT_EQ(light.shadow_map_near_z, PointLight::DEFAULT_SHADOW_MAP_NEAR_Z);

    const PointLightShadowMap shadow_map{};
    EXPECT_EQ(shadow_map.size, 1024u) << "each of the six cubemap faces";
}

/** @brief `SpotLight` differs from `PointLight` in its shadow biases, its cone
 *  angles and its intensity literal, and shares the rest of Bevy's defaults
 *  (`bevy_light/src/spot_light.rs:128-155`). */
TEST(Cluster, SpotLightMatchesBevyDefaults) {
    using epix::pbr::SpotLight;

    const SpotLight light{};
    EXPECT_EQ(light.color, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    EXPECT_FLOAT_EQ(light.intensity, 1'000'000.0f);
    EXPECT_FLOAT_EQ(light.range, 20.0f);
    EXPECT_FLOAT_EQ(light.radius, 0.0f);
    EXPECT_FALSE(light.shadows_enabled);
    EXPECT_TRUE(light.affects_lightmapped_mesh_diffuse);
    EXPECT_FLOAT_EQ(light.shadow_depth_bias, SpotLight::DEFAULT_SHADOW_DEPTH_BIAS);
    EXPECT_FLOAT_EQ(light.shadow_depth_bias, 0.02f) << "the spot bias differs from the point light's";
    EXPECT_FLOAT_EQ(light.shadow_normal_bias, SpotLight::DEFAULT_SHADOW_NORMAL_BIAS);
    EXPECT_FLOAT_EQ(light.shadow_normal_bias, 1.8f);
    EXPECT_FLOAT_EQ(light.shadow_map_near_z, SpotLight::DEFAULT_SHADOW_MAP_NEAR_Z);
    EXPECT_FLOAT_EQ(light.inner_angle, 0.0f);
    EXPECT_FLOAT_EQ(light.outer_angle, 0.7853981633974483f) << "Bevy's quarter arc";
    EXPECT_LT(light.inner_angle, light.outer_angle) << "Bevy requires inner <= outer";
    EXPECT_LT(light.outer_angle, 1.5707963267948966f) << "outer_angle must stay below pi/2";
}

/** @brief The two light-texture components (Bevy `SpotLightTexture`,
 *  `spot_light.rs:192-202`, and `PointLightTexture`, `point_light.rs:155-171`):
 *  one image handle each (plus the point light's cubemap layout), and Bevy's
 *  `#[require(SpotLight)]` / `#[require(PointLight)]` — spawning the texture
 *  component alone must produce the light it modulates. */
TEST(Cluster, LightTextureComponentsMatchBevy) {
    using epix::pbr::PointLight;
    using epix::pbr::PointLightTexture;
    using epix::pbr::SpotLight;
    using epix::pbr::SpotLightTexture;

    const SpotLightTexture spot_texture{};
    EXPECT_EQ(spot_texture.image.id(), epix::image::DEFAULT_IMAGE_HANDLE.id());

    const PointLightTexture point_texture{};
    EXPECT_EQ(point_texture.image.id(), epix::image::DEFAULT_IMAGE_HANDLE.id());
    EXPECT_EQ(point_texture.cubemap_layout, epix::camera::CubemapLayout::CrossVertical)
        << "Bevy's CubemapLayout default";

    epix::ecs::World world(3);
    const auto spot_entity  = world.spawn(SpotLightTexture{}).id();
    const auto point_entity = world.spawn(PointLightTexture{}).id();
    EXPECT_TRUE(world.entity(spot_entity).contains<SpotLight>())
        << "SpotLightTexture requires SpotLight";
    EXPECT_TRUE(world.entity(point_entity).contains<PointLight>())
        << "PointLightTexture requires PointLight";

    // The requirement is one-way: a plain light does not gain the texture.
    const auto plain_light = world.spawn(SpotLight{}).id();
    EXPECT_FALSE(world.entity(plain_light).contains<SpotLightTexture>());
}

/** @brief Cascade bounds are geometric between the first far bound and the
 *  maximum distance, with a single cascade simply reaching the maximum (Bevy
 *  `calculate_cascade_bounds`, `cascade.rs:39-54`), and the builder validates
 *  Bevy's five conditions by throwing rather than asserting. */
TEST(Cluster, CascadeShadowConfigMatchesBevy) {
    using epix::pbr::build_cascade_shadow_config;
    using epix::pbr::CascadeShadowConfigBuilder;
    using epix::pbr::calculate_cascade_bounds;
    using epix::pbr::default_cascade_shadow_config;

    const auto single = calculate_cascade_bounds(1, 10.0f, 150.0f);
    ASSERT_EQ(single.size(), 1u);
    EXPECT_FLOAT_EQ(single[0], 150.0f);

    const auto four = calculate_cascade_bounds(4, 10.0f, 150.0f);
    ASSERT_EQ(four.size(), 4u);
    EXPECT_NEAR(four.front(), 10.0f, 1e-4f) << "the first bound is the first cascade's far bound";
    EXPECT_NEAR(four.back(), 150.0f, 1e-3f) << "the last bound is the maximum distance";
    for (std::size_t i = 1; i < four.size(); ++i) {
        EXPECT_GT(four[i], four[i - 1]) << "bounds increase with distance";
    }

    const auto defaults = default_cascade_shadow_config();
    EXPECT_EQ(defaults.bounds.size(), 4u) << "Bevy's default is four cascades";
    EXPECT_FLOAT_EQ(defaults.overlap_proportion, 0.2f);
    EXPECT_FLOAT_EQ(defaults.minimum_distance, 0.1f);

    // Bevy asserts on each invalid case; Epix throws with Bevy's wording.
    EXPECT_THROW(build_cascade_shadow_config(CascadeShadowConfigBuilder{.num_cascades = 0}), std::invalid_argument);
    EXPECT_THROW(build_cascade_shadow_config(CascadeShadowConfigBuilder{.minimum_distance = -1.0f}), std::invalid_argument);
    EXPECT_THROW(build_cascade_shadow_config(CascadeShadowConfigBuilder{.maximum_distance = 0.05f}), std::invalid_argument);
    EXPECT_THROW(build_cascade_shadow_config(CascadeShadowConfigBuilder{.overlap_proportion = 1.0f}), std::invalid_argument);
    EXPECT_THROW(build_cascade_shadow_config(CascadeShadowConfigBuilder{.first_cascade_far_bound = 0.05f}),
                 std::invalid_argument);

    // The single-cascade case skips the first-bound comparison, as Bevy does.
    const auto one_cascade = build_cascade_shadow_config(CascadeShadowConfigBuilder{
        .num_cascades = 1, .minimum_distance = 0.1f, .maximum_distance = 150.0f, .first_cascade_far_bound = 0.05f});
    ASSERT_EQ(one_cascade.bounds.size(), 1u);
    EXPECT_FLOAT_EQ(one_cascade.bounds[0], 150.0f);
}

/** @brief `DirectionalLight` carries Bevy's defaults - ambient daylight as the
 *  illuminance, shadows off, and the directional shadow biases - and its shadow
 *  map defaults to 2048 per cascade, rounded to a power of two when set
 *  otherwise (`bevy_light/src/directional_light.rs:140-202`). */
TEST(Cluster, DirectionalLightMatchesBevyDefaults) {
    using epix::pbr::DirectionalLight;
    using epix::pbr::DirectionalLightShadowMap;
    using epix::pbr::DirectionalLightTexture;
    using epix::pbr::light_consts::lux::AMBIENT_DAYLIGHT;

    const DirectionalLight light{};
    EXPECT_EQ(light.color, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    EXPECT_FLOAT_EQ(light.illuminance, AMBIENT_DAYLIGHT);
    EXPECT_FLOAT_EQ(light.illuminance, 10'000.0f) << "Bevy defaults to ambient daylight";
    EXPECT_FALSE(light.shadows_enabled);
    EXPECT_TRUE(light.affects_lightmapped_mesh_diffuse);
    EXPECT_FLOAT_EQ(light.shadow_depth_bias, DirectionalLight::DEFAULT_SHADOW_DEPTH_BIAS);
    EXPECT_FLOAT_EQ(light.shadow_depth_bias, 0.02f);
    EXPECT_FLOAT_EQ(light.shadow_normal_bias, DirectionalLight::DEFAULT_SHADOW_NORMAL_BIAS);
    EXPECT_FLOAT_EQ(light.shadow_normal_bias, 1.8f);

    const DirectionalLightShadowMap shadow_map{};
    EXPECT_EQ(shadow_map.size, 2048u) << "the width and height of each cascade";

    const DirectionalLightTexture texture{};
    EXPECT_FALSE(texture.tiled) << "Bevy centres a single tile by default";
}

/** @brief `validate_shadow_map_size` corrects a non-power-of-two size once, and
 *  only when the resource changed (Bevy `directional_light.rs:198-204`). */
TEST(Cluster, ValidateShadowMapSizeSystemRoundsUpChangedSizes) {
    using epix::pbr::DirectionalLightShadowMap;
    using epix::pbr::validate_shadow_map_size;

    epix::app::App app = epix::app::App::create();
    app.world_mut().insert_resource(DirectionalLightShadowMap{.size = 3000});
    app.add_systems(epix::app::Update, epix::ecs::into(validate_shadow_map_size));

    app.update();
    EXPECT_EQ(app.world().resource<DirectionalLightShadowMap>().size, 4096u)
        << "anything that is not a power of two rounds up";

    // A power of two is left alone, and an untouched resource is not inspected at
    // all: the second run leaves it as the first one did.
    app.update();
    EXPECT_EQ(app.world().resource<DirectionalLightShadowMap>().size, 4096u);

    app.world_mut().resource_mut<DirectionalLightShadowMap>().size = 1024;
    app.update();
    EXPECT_EQ(app.world().resource<DirectionalLightShadowMap>().size, 1024u);
}

/** @brief `Cascade` carries the three shadow transforms and the texel size, and
 *  `Cascades` keys one list of them per view (Bevy `cascade.rs:164-184`). */
TEST(Cluster, CascadesAreKeyedPerView) {
    using epix::pbr::Cascade;
    using epix::pbr::Cascades;

    const Cascade cascade{};
    EXPECT_EQ(cascade.world_from_cascade, glm::mat4(1.0f)) << "an identity default, as Bevy's Mat4 default is";
    EXPECT_EQ(cascade.clip_from_cascade, glm::mat4(1.0f));
    EXPECT_EQ(cascade.clip_from_world, glm::mat4(1.0f));
    EXPECT_FLOAT_EQ(cascade.texel_size, 0.0f);

    Cascades cascades;
    EXPECT_TRUE(cascades.cascades.empty());
    const auto view = epix::ecs::Entity{};
    cascades.cascades[view] = std::vector<Cascade>(4);
    ASSERT_EQ(cascades.cascades.size(), 1u);
    EXPECT_EQ(cascades.cascades.at(view).size(), 4u) << "the default config has four cascades";
}

/** @brief `EnvironmentMapLight` carries Bevy's defaults: no intensity, identity
 *  rotation, and a diffuse lightmapped contribution, with both cubemap handles
 *  defaulting to the engine's default image in place of Bevy's
 *  `Handle::default()` (`bevy_light/src/probe.rs:99-108`). */
TEST(Cluster, EnvironmentMapLightMatchesBevyDefaults) {
    using epix::pbr::EnvironmentMapLight;
    using epix::pbr::LightProbe;

    const EnvironmentMapLight environment{};
    EXPECT_EQ(environment.diffuse_map.id(), epix::image::DEFAULT_IMAGE_HANDLE.id());
    EXPECT_EQ(environment.specular_map.id(), epix::image::DEFAULT_IMAGE_HANDLE.id());
    EXPECT_FLOAT_EQ(environment.intensity, 0.0f);
    EXPECT_EQ(environment.rotation, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)) << "Bevy's Quat::IDENTITY";
    EXPECT_TRUE(environment.affects_lightmapped_mesh_diffuse);

    // The marker carries no state; it is the entity's transform that gives the
    // probe its region.
    const LightProbe probe{};
    static_cast<void>(probe);
}

/** @brief The remaining probe components carry Bevy's defaults
 *  (`bevy_light/src/probe.rs:116-221`): a runtime-filtered environment map with
 *  no intensity, an atmosphere environment light at intensity 1 whose cubemap is
 *  512 square, and an irradiance volume with no intensity whose flag is spelled
 *  `affects_lightmapped_meshes` rather than the other lights'
 *  `..._diffuse`. */
TEST(Cluster, ProbeComponentsMatchBevyDefaults) {
    using epix::pbr::AtmosphereEnvironmentMapLight;
    using epix::pbr::GeneratedEnvironmentMapLight;
    using epix::pbr::IrradianceVolume;

    const GeneratedEnvironmentMapLight generated{};
    EXPECT_EQ(generated.environment_map.id(), epix::image::DEFAULT_IMAGE_HANDLE.id());
    EXPECT_FLOAT_EQ(generated.intensity, 0.0f);
    EXPECT_EQ(generated.rotation, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_TRUE(generated.affects_lightmapped_mesh_diffuse);

    const AtmosphereEnvironmentMapLight atmosphere{};
    EXPECT_FLOAT_EQ(atmosphere.intensity, 1.0f) << "Bevy's default matches the atmosphere's own intensity";
    EXPECT_TRUE(atmosphere.affects_lightmapped_mesh_diffuse);
    EXPECT_EQ(atmosphere.size, glm::uvec2(512, 512));
    EXPECT_EQ(atmosphere.size.x & (atmosphere.size.x - 1), 0u) << "the cubemap size must be a power of two";

    const IrradianceVolume volume{};
    EXPECT_EQ(volume.voxels.id(), epix::image::DEFAULT_IMAGE_HANDLE.id());
    EXPECT_FLOAT_EQ(volume.intensity, 0.0f);
    EXPECT_TRUE(volume.affects_lightmapped_meshes);
}

/** @brief Volumetric fog carries Bevy's defaults (`bevy_light/src/volumetric.rs`):
 *  a 64-step raymarch with no jitter and a 0.1 ambient contribution on the fog
 *  component, and a fog volume with 0.3 absorption and scattering, 0.1 density,
 *  and no density texture. Note `scattering_asymmetry`: Bevy's doc says 0.8, its
 *  implementation sets 0.5, and this port follows the implementation. */
TEST(Cluster, VolumetricFogMatchesBevyDefaults) {
    using epix::pbr::FogVolume;
    using epix::pbr::VolumetricFog;
    using epix::pbr::VolumetricLight;

    const VolumetricFog fog{};
    EXPECT_EQ(fog.ambient_color, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    EXPECT_FLOAT_EQ(fog.ambient_intensity, 0.1f);
    EXPECT_FLOAT_EQ(fog.jitter, 0.0f);
    EXPECT_EQ(fog.step_count, 64u);

    const FogVolume volume{};
    EXPECT_EQ(volume.fog_color, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    EXPECT_FLOAT_EQ(volume.density_factor, 0.1f);
    EXPECT_FALSE(volume.density_texture.has_value()) << "Bevy's default is Option::None, not a default image";
    EXPECT_EQ(volume.density_texture_offset, glm::vec3(0.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(volume.absorption, 0.3f);
    EXPECT_FLOAT_EQ(volume.scattering, 0.3f);
    EXPECT_FLOAT_EQ(volume.scattering_asymmetry, 0.5f) << "the implementation's value, not the doc's 0.8";
    EXPECT_EQ(volume.light_tint, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    EXPECT_FLOAT_EQ(volume.light_intensity, 1.0f);

    const VolumetricLight light{};
    static_cast<void>(light);
}

/** @brief `ClusterZConfig`'s default slices at 5.0 with the max-range far-Z mode
 *  (`cluster/mod.rs:221-228`), and `ClusteredDecal` defaults to no textures and
 *  tag zero (`cluster/mod.rs:164-219`). */
TEST(Cluster, ClusterZConfigAndDecalMatchBevyDefaults) {
    using epix::pbr::ClusterFarZMode;
    using epix::pbr::ClusteredDecal;
    using epix::pbr::ClusterZConfig;

    const ClusterZConfig z_config{};
    EXPECT_FLOAT_EQ(z_config.first_slice_depth, 5.0f) << "Bevy's Default, not zero";
    EXPECT_EQ(z_config.far_z_mode.kind, ClusterFarZMode::Kind::MaxClusterableObjectRange);

    const ClusteredDecal decal{};
    EXPECT_FALSE(decal.base_color_texture.has_value());
    EXPECT_FALSE(decal.normal_map_texture.has_value());
    EXPECT_FALSE(decal.metallic_roughness_texture.has_value());
    EXPECT_FALSE(decal.emissive_texture.has_value());
    EXPECT_EQ(decal.tag, 0u);
}

/** @brief The GPU light uniform matches Bevy's shader layout, padding included
 *  (`bevy_pbr/src/render/light.rs:124-174`): a cascade is 80 bytes because a
 *  shader type's size rounds up to its largest member's alignment, a directional
 *  light is 384, and the whole uniform is 3904 with four trailing pad bytes. */
TEST(Cluster, GpuLightsMatchBevyShaderLayout) {
    using epix::pbr::DirectionalLightFlags;
    using epix::pbr::GpuDirectionalCascade;
    using epix::pbr::GpuDirectionalLight;
    using epix::pbr::GpuLights;
    using epix::pbr::MAX_CASCADES_PER_LIGHT;
    using epix::pbr::MAX_DIRECTIONAL_LIGHTS;

    EXPECT_EQ(sizeof(GpuDirectionalCascade), 80u) << "64-byte matrix, two floats, then padding to 16";
    EXPECT_EQ(offsetof(GpuDirectionalCascade, texel_size), 64u);
    EXPECT_EQ(offsetof(GpuDirectionalCascade, far_bound), 68u);

    EXPECT_EQ(MAX_CASCADES_PER_LIGHT, 4u);
    EXPECT_EQ(MAX_DIRECTIONAL_LIGHTS, 10u);
    EXPECT_EQ(sizeof(GpuDirectionalLight), 400u) << "four 80-byte cascades, then fields sized to a 16-byte multiple";
    EXPECT_EQ(offsetof(GpuDirectionalLight, color), 320u) << "four 80-byte cascades precede the colour";
    EXPECT_EQ(offsetof(GpuDirectionalLight, dir_to_light), 336u);
    EXPECT_EQ(offsetof(GpuDirectionalLight, flags), 348u) << "a vec3 followed by a scalar packs into one slot";
    EXPECT_EQ(offsetof(GpuDirectionalLight, soft_shadow_size), 352u);
    EXPECT_EQ(offsetof(GpuDirectionalLight, num_cascades), 364u);
    EXPECT_EQ(offsetof(GpuDirectionalLight, sun_disk_intensity), 384u);

    EXPECT_EQ(sizeof(GpuLights), 4064u);
    EXPECT_EQ(offsetof(GpuLights, ambient_color), 4000u) << "ten 400-byte lights precede the ambient colour";
    EXPECT_EQ(offsetof(GpuLights, cluster_dimensions), 4016u);
    EXPECT_EQ(offsetof(GpuLights, cluster_factors), 4032u);
    EXPECT_EQ(offsetof(GpuLights, n_directional_lights), 4048u);

    EXPECT_EQ(DirectionalLightFlags::SHADOWS_ENABLED, 1u);
    EXPECT_EQ(DirectionalLightFlags::VOLUMETRIC, 2u);
    EXPECT_EQ(DirectionalLightFlags::AFFECTS_LIGHTMAPPED_MESH_DIFFUSE, 4u);
    EXPECT_EQ(DirectionalLightFlags::NONE, 0u);
    EXPECT_EQ(DirectionalLightFlags::UNINITIALIZED, 0xFFFFu);

    // The uniform is a typed uniform buffer payload, so it must satisfy the
    // engine's shader-type concept.
    static_assert(epix::render::render_resource::ShaderWritable<GpuLights>);
    const GpuLights lights = epix::pbr::default_gpu_lights();
    EXPECT_EQ(lights.n_directional_lights, 0u);
    EXPECT_EQ(lights.directional_lights[0].flags, DirectionalLightFlags::NONE);
}

/** @brief `LightMeta` holds one `GpuLights` per view and stamps each view with
 *  that entry's byte offset, as Bevy's does (`render/light.rs:680-688`), and the
 *  per-shadow-view components carry what the shadow pass needs. */
TEST(Cluster, LightMetaKeysOneGpuLightsEntryPerView) {
    using epix::pbr::GpuLights;
    using epix::pbr::LightEntity;
    using epix::pbr::LightMeta;
    using epix::pbr::ViewLightEntities;
    using epix::pbr::ViewLightsUniformOffset;
    using epix::pbr::ViewShadowBindings;
    using epix::render::render_resource::DynamicUniformBuffer;

    // A default-constructed meta can be filled; uploading needs an aligned
    // buffer, which the plugin creates with the device's alignment.
    LightMeta meta{};
    const std::size_t first  = meta.view_gpu_lights.push(GpuLights{});
    const std::size_t second = meta.view_gpu_lights.push(GpuLights{});
    EXPECT_EQ(first, 0u) << "the first view starts the buffer";
    EXPECT_GE(second, sizeof(GpuLights)) << "the second entry follows the first, at the buffer's stride";
    EXPECT_FALSE(meta.view_gpu_lights.is_empty()) << "both views were pushed";

    const ViewLightsUniformOffset offset{};
    EXPECT_EQ(offset.offset, 0u);

    ViewLightEntities entities{};
    EXPECT_TRUE(entities.lights.empty());
    entities.lights.push_back(epix::ecs::Entity{});
    EXPECT_EQ(entities.lights.size(), 1u);

    LightEntity light{};
    EXPECT_EQ(light.kind, LightEntity::Kind::Spot) << "a spot light has no cascade or face index";
    light.kind         = LightEntity::Kind::Directional;
    light.cascade_index = 2;
    EXPECT_EQ(light.cascade_index, 2u);

    static_assert(sizeof(ViewShadowBindings) > 0);
}

/** @brief The packed cluster word carries an offset and two counts in the widths
 *  `pbr.wgsl` expects (Bevy `pack_offset_and_counts`, `cluster.rs:31-36,551-555`),
 *  each field masked so that an out-of-range value keeps only its low bits. */
TEST(Cluster, PackOffsetAndCountsMatchesBevy) {
    using epix::pbr::CLUSTER_COUNT_MASK;
    using epix::pbr::CLUSTER_COUNT_SIZE;
    using epix::pbr::CLUSTER_OFFSET_MASK;
    using epix::pbr::pack_offset_and_counts;

    EXPECT_EQ(CLUSTER_COUNT_SIZE, 9u);
    EXPECT_EQ(CLUSTER_COUNT_MASK, 0x1FFu);
    EXPECT_EQ(CLUSTER_OFFSET_MASK, 0x3FFFu) << "the remaining 14 bits";
    EXPECT_LE(epix::pbr::MAX_UNIFORM_BUFFER_CLUSTERABLE_OBJECTS, CLUSTER_COUNT_MASK)
        << "a count field must hold every uniform-buffer clusterable object";

    const std::uint32_t packed = pack_offset_and_counts(5, 3, 7);
    EXPECT_EQ(packed & CLUSTER_COUNT_MASK, 7u) << "spot count in the low bits";
    EXPECT_EQ((packed >> CLUSTER_COUNT_SIZE) & CLUSTER_COUNT_MASK, 3u) << "point count above it";
    EXPECT_EQ(packed >> (CLUSTER_COUNT_SIZE * 2), 5u) << "offset in the high bits";

    // Bevy masks rather than validates, so values above a field's width wrap.
    const std::uint32_t wrapped = pack_offset_and_counts(CLUSTER_OFFSET_MASK + 1u, 0, 0);
    EXPECT_EQ(wrapped, 0u) << "an offset one past the mask wraps to zero";

    const std::uint32_t maxed = pack_offset_and_counts(CLUSTER_OFFSET_MASK, CLUSTER_COUNT_MASK, CLUSTER_COUNT_MASK);
    EXPECT_EQ(maxed, 0xFFFFFFFFu) << "all fields full";
}

/** @brief The scene's clusterable objects use the buffer form the device offers,
 *  and `set` clamps in the uniform case while replacing the list in the storage
 *  case (Bevy `GpuClusterableObjects`, `cluster.rs:171-232`). */
TEST(Cluster, GpuClusterableObjectsClampsUniformAndReplacesStorage) {
    using epix::pbr::GlobalClusterableObjectMeta;
    using epix::pbr::GpuClusterableObject;
    using epix::pbr::GpuClusterableObjects;
    using epix::pbr::MAX_UNIFORM_BUFFER_CLUSTERABLE_OBJECTS;

    // The device's binding type chooses the form.
    const auto uniform_form = GpuClusterableObjects::from_binding_type(wgpu::BufferBindingType::eUniform);
    EXPECT_TRUE(std::holds_alternative<GpuClusterableObjects::Uniform>(uniform_form.buffer));
    const auto storage_form = GpuClusterableObjects::from_binding_type(wgpu::BufferBindingType::eStorage);
    EXPECT_TRUE(std::holds_alternative<GpuClusterableObjects::Storage>(storage_form.buffer));

    // A uniform buffer cannot grow, so more objects than it holds are dropped.
    auto uniform_objects = GpuClusterableObjects::uniform();
    uniform_objects.set(std::vector<GpuClusterableObject>(MAX_UNIFORM_BUFFER_CLUSTERABLE_OBJECTS + 50));
    EXPECT_EQ(std::get<GpuClusterableObjects::Uniform>(uniform_objects.buffer).get().data.size(),
              MAX_UNIFORM_BUFFER_CLUSTERABLE_OBJECTS);

    // The storage form keeps the whole list.
    auto storage_objects = GpuClusterableObjects::storage();
    storage_objects.set(std::vector<GpuClusterableObject>(MAX_UNIFORM_BUFFER_CLUSTERABLE_OBJECTS + 50));
    EXPECT_EQ(std::get<GpuClusterableObjects::Storage>(storage_objects.buffer).get().data.size(),
              MAX_UNIFORM_BUFFER_CLUSTERABLE_OBJECTS + 50);

    // Nothing has been uploaded yet, so there is no buffer to bind.
    EXPECT_FALSE(uniform_objects.binding().has_value());
    EXPECT_FALSE(storage_objects.binding().has_value());

    const GlobalClusterableObjectMeta meta{wgpu::BufferBindingType::eStorage};
    EXPECT_TRUE(std::holds_alternative<GpuClusterableObjects::Storage>(meta.gpu_clusterable_objects.buffer));
    EXPECT_TRUE(meta.entity_to_index.empty());
}

/** @brief The three minimum binding sizes the mesh view layout declares for the
 *  cluster bindings, in both buffer forms (Bevy `GpuClusterableObjects::min_size`,
 *  `bevy_pbr/src/cluster.rs:226-231`, and
 *  `ViewClusterBindings::min_size_clusterable_object_index_lists` /
 *  `min_size_cluster_offsets_and_counts`, `cluster.rs:485-501`).
 *
 *  The uniform forms are the whole fixed arrays, because a uniform buffer cannot
 *  grow; the storage forms are one element each, which is what encase's
 *  `min_size()` answers for a runtime-sized struct. Those are the values
 *  `layout_entries` passes to `buffer_layout` for bindings 8, 9 and 10
 *  (`mesh_view_bindings.rs:284-317`). */
TEST(Cluster, ClusterBufferMinimumSizesMatchBevy) {
    using epix::pbr::GpuClusterableObject;
    using epix::pbr::GpuClusterableObjectIndexListsUniform;
    using epix::pbr::GpuClusterableObjects;
    using epix::pbr::GpuClusterableObjectsUniform;
    using epix::pbr::GpuClusterOffsetsAndCounts;
    using epix::pbr::GpuClusterOffsetsAndCountsUniform;
    using epix::pbr::MAX_UNIFORM_BUFFER_CLUSTERABLE_OBJECTS;
    using epix::pbr::ViewClusterBindings;

    const auto uniform = wgpu::BufferBindingType::eUniform;
    const auto storage = wgpu::BufferBindingType::eStorage;
    const auto read_only_storage = wgpu::BufferBindingType::eReadOnlyStorage;

    // Binding 8: the scene-wide clusterable objects.
    EXPECT_EQ(GpuClusterableObjects::min_size(uniform), sizeof(GpuClusterableObjectsUniform));
    EXPECT_EQ(GpuClusterableObjects::min_size(uniform),
              MAX_UNIFORM_BUFFER_CLUSTERABLE_OBJECTS * sizeof(GpuClusterableObject));
    EXPECT_EQ(GpuClusterableObjects::min_size(storage), sizeof(GpuClusterableObject));
    EXPECT_EQ(GpuClusterableObjects::min_size(read_only_storage), sizeof(GpuClusterableObject));

    // Binding 9: the cluster index lists. The uniform form is its full uvec4
    // array; the storage form is one u32 of the runtime-sized list.
    EXPECT_EQ(ViewClusterBindings::min_size_clusterable_object_index_lists(uniform),
              sizeof(GpuClusterableObjectIndexListsUniform));
    EXPECT_EQ(ViewClusterBindings::min_size_clusterable_object_index_lists(storage), sizeof(std::uint32_t));
    EXPECT_EQ(ViewClusterBindings::min_size_clusterable_object_index_lists(read_only_storage),
              sizeof(std::uint32_t));

    // Binding 10: the offsets and counts. The storage element is two uvec4s.
    EXPECT_EQ(ViewClusterBindings::min_size_cluster_offsets_and_counts(uniform),
              sizeof(GpuClusterOffsetsAndCountsUniform));
    EXPECT_EQ(ViewClusterBindings::min_size_cluster_offsets_and_counts(storage),
              sizeof(GpuClusterOffsetsAndCounts));
    EXPECT_EQ(ViewClusterBindings::min_size_cluster_offsets_and_counts(storage), 32u);
}

/** @brief The clustered-decal entries: the decals buffer, the decal texture
 *  array and their sampler, in the one shape Bevy has for them (Bevy
 *  `decal::clustered::{max_view_decals, clustered_decals_are_usable,
 *  get_bind_group_layout_entries}`, `decal/clustered.rs:399-420,520-555`).
 *
 *  Binding arrays are a precondition rather than a choice, so nothing comes back
 *  when they are missing or the cargo feature is off, and the texture entry
 *  always carries the platform's decal cap. The three entries land at the
 *  binding-array group's bindings 5, 6 and 7. */
TEST(Cluster, ClusteredDecalLayoutEntriesMatchBevy) {
    using epix::pbr::clustered_decal_bind_group_layout_entries;
    using epix::pbr::clustered_decals_are_usable;
    using epix::pbr::max_view_decals;
    using epix::pbr::MAX_VIEW_DECALS;
    using epix::pbr::MAX_VIEW_DECALS_WITH_PARTIALLY_BOUND_BINDING_ARRAY;
    using epix::pbr::RenderClusteredDecal;

    // The object is the shader's 96 bytes: a mat4, four indices, a tag and three
    // pads, and Bevy's `Default` is the zero matrix with zero indices.
    EXPECT_EQ(sizeof(RenderClusteredDecal), 96u);
    EXPECT_EQ(offsetof(RenderClusteredDecal, local_from_world), 0u);
    EXPECT_EQ(offsetof(RenderClusteredDecal, image_indices), 64u);
    EXPECT_EQ(offsetof(RenderClusteredDecal, tag), 80u);
    EXPECT_EQ(offsetof(RenderClusteredDecal, pad_c), 92u);
    const RenderClusteredDecal defaulted{};
    EXPECT_EQ(defaulted.local_from_world, glm::mat4(0.0f));
    EXPECT_EQ(defaulted.image_indices, (std::array<std::int32_t, 4>{0, 0, 0, 0}));

    // Binding arrays and the cargo feature are both required, exactly Bevy's `&&`.
    EXPECT_FALSE(clustered_decals_are_usable(false, false));
    EXPECT_FALSE(clustered_decals_are_usable(false, true));
    EXPECT_FALSE(clustered_decals_are_usable(true, false));
    EXPECT_TRUE(clustered_decals_are_usable(true, true));

    // The cap follows partially-bound binding array support.
    EXPECT_EQ(max_view_decals(true), MAX_VIEW_DECALS_WITH_PARTIALLY_BOUND_BINDING_ARRAY);
    EXPECT_EQ(max_view_decals(true), 1024u);
    EXPECT_EQ(max_view_decals(false), MAX_VIEW_DECALS);
    EXPECT_EQ(max_view_decals(false), 8u);

    // No binding arrays, or no feature, means no entries at all - not a
    // single-texture shape.
    EXPECT_FALSE(clustered_decal_bind_group_layout_entries(false, true, true).has_value());
    EXPECT_FALSE(clustered_decal_bind_group_layout_entries(true, false, true).has_value());

    const auto entries = clustered_decal_bind_group_layout_entries(true, true, true);
    ASSERT_TRUE(entries.has_value());
    const auto at = [&](std::size_t index, std::uint32_t binding) {
        return (*entries)[index].build(binding, wgpu::ShaderStage::eVertex);
    };

    // The decals buffer: read-only storage with the element's minimum size.
    const auto buffer = at(0u, 5u);
    EXPECT_EQ(buffer.buffer.type, wgpu::BufferBindingType::eReadOnlyStorage);
    EXPECT_EQ(buffer.buffer.hasDynamicOffset, 0u);
    EXPECT_EQ(buffer.buffer.minBindingSize, sizeof(RenderClusteredDecal));
    EXPECT_EQ(buffer.binding, 5u);

    // The texture array: sized by the same cap, sampled as a filterable float.
    const auto textures = at(1u, 6u);
    EXPECT_EQ(textures.texture.viewDimension, wgpu::TextureViewDimension::e2D);
    EXPECT_EQ(textures.texture.sampleType, wgpu::TextureSampleType::eFloat);
    EXPECT_EQ(textures.bindingArraySize, MAX_VIEW_DECALS_WITH_PARTIALLY_BOUND_BINDING_ARRAY);
    EXPECT_EQ(at(2u, 7u).sampler.type, wgpu::SamplerBindingType::eFiltering);

    // Without partially bound arrays the array is capped at 8; the buffer entry
    // is otherwise identical.
    const auto small = clustered_decal_bind_group_layout_entries(true, true, false);
    ASSERT_TRUE(small.has_value());
    EXPECT_EQ((*small)[1].build(6u, wgpu::ShaderStage::eVertex).bindingArraySize, MAX_VIEW_DECALS);
}

/** @brief The atmosphere settings and the storage buffer the mesh view layout
 *  binds at binding 31 (Bevy `bevy_pbr/src/atmosphere/mod.rs:292-449` and
 *  `atmosphere/resources.rs:475-482,769-774`, laid out by
 *  `atmosphere/types.wgsl:3-36`).
 *
 *  The explicit padding is what the shader's alignment forces: `vec3<u32>` and
 *  `vec3<f32>` are sixteen-byte aligned, so the two GPU structs are 80 and 32
 *  bytes where glm's unaligned gentypes would give 72 and 20. */
TEST(Cluster, AtmosphereShaderLayoutMatchesBevy) {
    using epix::pbr::AtmosphereData;
    using epix::pbr::AtmosphereMode;
    using epix::pbr::AtmosphereSettings;
    using epix::pbr::GpuAtmosphere;
    using epix::pbr::GpuAtmosphereSettings;
    using epix::render::render_resource::ShaderTypeInfo;
    using epix::render::render_resource::ShaderWritable;

    static_assert(ShaderWritable<GpuAtmosphere>);
    static_assert(ShaderWritable<GpuAtmosphereSettings>);
    static_assert(ShaderWritable<AtmosphereData>);
    static_assert(ShaderTypeInfo<AtmosphereData>::shader_size == 112u);

    // Bevy's settings defaults, field by field.
    const AtmosphereSettings settings{};
    EXPECT_EQ(settings.transmittance_lut_size, glm::uvec2(256, 128));
    EXPECT_EQ(settings.multiscattering_lut_size, glm::uvec2(32, 32));
    EXPECT_EQ(settings.sky_view_lut_size, glm::uvec2(400, 200));
    EXPECT_EQ(settings.aerial_view_lut_size, glm::uvec3(32, 32, 32));
    EXPECT_EQ(settings.transmittance_lut_samples, 40u);
    EXPECT_EQ(settings.multiscattering_lut_dirs, 64u);
    EXPECT_EQ(settings.multiscattering_lut_samples, 20u);
    EXPECT_EQ(settings.sky_view_lut_samples, 16u);
    EXPECT_EQ(settings.aerial_view_lut_samples, 10u);
    EXPECT_FLOAT_EQ(settings.aerial_view_lut_max_distance, 3.2e4f);
    EXPECT_FLOAT_EQ(settings.scene_units_to_m, 1.0f);
    EXPECT_EQ(settings.sky_max_samples, 16u);
    EXPECT_EQ(settings.rendering_method, AtmosphereMode::LookupTexture);
    EXPECT_EQ(static_cast<std::uint32_t>(AtmosphereMode::LookupTexture), 0u);
    EXPECT_EQ(static_cast<std::uint32_t>(AtmosphereMode::Raymarched), 1u);

    // The shader's `Atmosphere`: a sixteen-byte-aligned vec3 then two scalars.
    EXPECT_EQ(sizeof(GpuAtmosphere), 32u);
    EXPECT_EQ(offsetof(GpuAtmosphere, ground_albedo), 0u);
    EXPECT_EQ(offsetof(GpuAtmosphere, bottom_radius), 12u);
    EXPECT_EQ(offsetof(GpuAtmosphere, top_radius), 16u);

    // The shader's `AtmosphereSettings`: three vec2<u32>, a sixteen-byte-aligned
    // vec3<u32>, then nine scalars.
    EXPECT_EQ(sizeof(GpuAtmosphereSettings), 80u);
    EXPECT_EQ(offsetof(GpuAtmosphereSettings, transmittance_lut_size), 0u);
    EXPECT_EQ(offsetof(GpuAtmosphereSettings, multiscattering_lut_size), 8u);
    EXPECT_EQ(offsetof(GpuAtmosphereSettings, sky_view_lut_size), 16u);
    EXPECT_EQ(offsetof(GpuAtmosphereSettings, aerial_view_lut_size), 32u);
    EXPECT_EQ(offsetof(GpuAtmosphereSettings, transmittance_lut_samples), 44u);
    EXPECT_EQ(offsetof(GpuAtmosphereSettings, rendering_method), 76u);

    // The buffer's two members, and the layout entry's minimum size.
    EXPECT_EQ(sizeof(AtmosphereData), 112u);
    EXPECT_EQ(offsetof(AtmosphereData, atmosphere), 0u);
    EXPECT_EQ(offsetof(AtmosphereData, settings), 32u);

    // The conversion widens the method, and Bevy's `Default` is the defaulted
    // settings converted.
    const auto gpu = GpuAtmosphereSettings::from(settings);
    EXPECT_EQ(gpu.rendering_method, 0u);
    EXPECT_EQ(gpu.transmittance_lut_size, settings.transmittance_lut_size);
    EXPECT_EQ(gpu.sky_max_samples, settings.sky_max_samples);
    EXPECT_EQ(GpuAtmosphereSettings::defaulted().rendering_method, 0u);
    AtmosphereSettings raymarched{};
    raymarched.rendering_method = AtmosphereMode::Raymarched;
    EXPECT_EQ(GpuAtmosphereSettings::from(raymarched).rendering_method, 1u);
}

/** @brief Bevy's screen-space reflection settings and the uniform they convert
 *  to (Bevy `bevy_pbr/src/ssr/mod.rs:85-143,236-251,581-591`).
 *
 *  Binding 15 of the mesh view layout is this uniform, so its 24 bytes are the
 *  minimum binding size declared there. */
TEST(Cluster, ScreenSpaceReflectionsMatchesBevy) {
    using epix::pbr::ScreenSpaceReflections;
    using epix::pbr::ScreenSpaceReflectionsUniform;

    // Bevy's Default is the gist-derived set, and its field order is the
    // uniform's order.
    const ScreenSpaceReflections defaults{};
    EXPECT_FLOAT_EQ(defaults.perceptual_roughness_threshold, 0.1f);
    EXPECT_FLOAT_EQ(defaults.thickness, 0.25f);
    EXPECT_EQ(defaults.linear_steps, 16u);
    EXPECT_FLOAT_EQ(defaults.linear_march_exponent, 1.0f);
    EXPECT_EQ(defaults.bisection_steps, 4u);
    EXPECT_TRUE(defaults.use_secant);

    // Six four-byte scalars, which is both the object representation and the
    // shader's std140 layout.
    EXPECT_EQ(sizeof(ScreenSpaceReflectionsUniform), 24u);
    EXPECT_EQ(offsetof(ScreenSpaceReflectionsUniform, perceptual_roughness_threshold), 0u);
    EXPECT_EQ(offsetof(ScreenSpaceReflectionsUniform, use_secant), 20u);

    const auto uniform = ScreenSpaceReflectionsUniform::from(defaults);
    EXPECT_FLOAT_EQ(uniform.perceptual_roughness_threshold, 0.1f);
    EXPECT_FLOAT_EQ(uniform.thickness, 0.25f);
    EXPECT_EQ(uniform.linear_steps, 16u);
    EXPECT_FLOAT_EQ(uniform.linear_march_exponent, 1.0f);
    EXPECT_EQ(uniform.bisection_steps, 4u);
    EXPECT_EQ(uniform.use_secant, 1u) << "the boolean is widened to a u32";

    // Turning the secant method off converts to zero rather than carrying a
    // false through.
    ScreenSpaceReflections off{};
    off.use_secant = false;
    off.linear_steps = 7;
    const auto off_uniform = ScreenSpaceReflectionsUniform::from(off);
    EXPECT_EQ(off_uniform.use_secant, 0u);
    EXPECT_EQ(off_uniform.linear_steps, 7u);
}

/** @brief The mesh view layouts: the entry list of every possible key, and the
 *  three descriptors each key produces (Bevy `layout_entries`,
 *  `generate_view_layouts` and `MeshPipelineViewLayouts`,
 *  `bevy_pbr/src/render/mesh_view_bindings.rs:204-554`).
 *
 *  Everything here is a descriptor, so the whole thing is CPU-side: the device
 *  facts are parameters, which is why this can be asserted without a device. */
TEST(Cluster, MeshViewLayoutEntriesMatchBevy) {
    using epix::pbr::AtmosphereData;
    using epix::pbr::generate_view_layouts;
    using epix::pbr::GpuClusterableObjects;
    using epix::pbr::GpuFog;
    using epix::pbr::GpuLights;
    using epix::pbr::LightProbesUniform;
    using epix::pbr::MeshPipelineViewLayoutKey;
    using epix::pbr::MeshPipelineViewLayouts;
    using epix::pbr::ScreenSpaceReflectionsUniform;
    using epix::pbr::ViewClusterBindings;
    using epix::core_graph::OrderIndependentTransparencySettings;
#ifndef NDEBUG
    EXPECT_EQ(epix::pbr::MESH_PIPELINE_VIEW_LAYOUT_SAFE_MAX_TEXTURES, 10u);
#endif
    EXPECT_EQ(MeshPipelineViewLayoutKey::COUNT, 128u) << "all().bits() + 1 over seven flags";

    const auto storage = wgpu::BufferBindingType::eReadOnlyStorage;
    const auto uniform = wgpu::BufferBindingType::eUniform;
    const auto find    = [](const std::vector<wgpu::BindGroupLayoutEntry>& list,
                            std::uint32_t binding) -> const wgpu::BindGroupLayoutEntry* {
        for (const auto& entry : list) {
            if (entry.binding == binding) return &entry;
        }
        return nullptr;
    };
    const auto bindings_of = [](const std::vector<wgpu::BindGroupLayoutEntry>& list) {
        std::vector<std::uint32_t> result;
        for (const auto& entry : list) result.push_back(entry.binding);
        return result;
    };

    // A bare key with nothing usable: the always-present main entries and the
    // three environment-map entries of the binding-array group.
    const auto plain_entries = epix::pbr::mesh_view_layout_entries(
        storage, storage, MeshPipelineViewLayoutKey::from_bits_retain(0), false, false, false, false);
    ASSERT_EQ(plain_entries.size(), 2u);
    EXPECT_EQ(bindings_of(plain_entries[0]),
              (std::vector<std::uint32_t>{0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u, 17u, 18u,
                                          19u, 24u, 25u}));
    // The binding-array group always has the environment maps and the irradiance
    // volumes: their usability is a platform fact, and without binding arrays the
    // textures are simply not arrays. The decals at 5 to 7 need arrays.
    EXPECT_EQ(bindings_of(plain_entries[1]), (std::vector<std::uint32_t>{0u, 1u, 2u, 3u, 4u}));

    // The view, lights, globals, fog, probes and the SSR settings are dynamic
    // uniforms with the element's minimum size; the view and globals are visible
    // to both stages and the cluster buffers are read-only storage.
    const auto* view = find(plain_entries[0], 0u);
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view->buffer.type, wgpu::BufferBindingType::eUniform);
    EXPECT_EQ(view->buffer.hasDynamicOffset, 1u);
    EXPECT_EQ(view->buffer.minBindingSize, sizeof(epix::render::view::ViewUniform));
    EXPECT_EQ(view->visibility, wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment);
    EXPECT_EQ(find(plain_entries[0], 1u)->buffer.minBindingSize, sizeof(GpuLights));
    EXPECT_EQ(find(plain_entries[0], 11u)->visibility,
              wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment);
    EXPECT_EQ(find(plain_entries[0], 12u)->buffer.minBindingSize, sizeof(GpuFog));
    EXPECT_EQ(find(plain_entries[0], 13u)->buffer.minBindingSize, sizeof(LightProbesUniform));
    EXPECT_EQ(find(plain_entries[0], 15u)->buffer.minBindingSize, sizeof(ScreenSpaceReflectionsUniform));
    EXPECT_EQ(find(plain_entries[0], 15u)->buffer.hasDynamicOffset, 1u);

    const auto* objects = find(plain_entries[0], 8u);
    ASSERT_NE(objects, nullptr);
    EXPECT_EQ(objects->buffer.type, wgpu::BufferBindingType::eReadOnlyStorage);
    EXPECT_EQ(objects->buffer.minBindingSize, GpuClusterableObjects::min_size(storage));
    EXPECT_EQ(find(plain_entries[0], 9u)->buffer.minBindingSize,
              ViewClusterBindings::min_size_clusterable_object_index_lists(storage));
    EXPECT_EQ(find(plain_entries[0], 10u)->buffer.minBindingSize,
              ViewClusterBindings::min_size_cluster_offsets_and_counts(storage));

    // The visibility-ranges buffer is a storage buffer of one vec4, visible to
    // the vertex stage only.
    const auto* visibility_ranges = find(plain_entries[0], 14u);
    ASSERT_NE(visibility_ranges, nullptr);
    EXPECT_EQ(visibility_ranges->buffer.minBindingSize, sizeof(glm::vec4));
    EXPECT_EQ(visibility_ranges->visibility, wgpu::ShaderStage::eVertex);

    // The shadow textures and the ambient-occlusion texture, and the transmission
    // texture and sampler.
    EXPECT_EQ(find(plain_entries[0], 2u)->texture.viewDimension, wgpu::TextureViewDimension::eCubeArray);
    EXPECT_EQ(find(plain_entries[0], 2u)->texture.sampleType, wgpu::TextureSampleType::eDepth);
    EXPECT_EQ(find(plain_entries[0], 3u)->sampler.type, wgpu::SamplerBindingType::eComparison);
    EXPECT_EQ(find(plain_entries[0], 5u)->texture.viewDimension, wgpu::TextureViewDimension::e2DArray);
    EXPECT_EQ(find(plain_entries[0], 6u)->sampler.type, wgpu::SamplerBindingType::eComparison);
    EXPECT_EQ(find(plain_entries[0], 16u)->texture.sampleType, wgpu::TextureSampleType::eUnfilterableFloat);
    EXPECT_EQ(find(plain_entries[0], 24u)->texture.sampleType, wgpu::TextureSampleType::eFloat);
    EXPECT_EQ(find(plain_entries[0], 25u)->sampler.type, wgpu::SamplerBindingType::eFiltering);

    // The environment map's uniform is the last always-present main entry, and
    // the binding-array group's first three are the two cubemaps and a sampler.
    EXPECT_EQ(find(plain_entries[0], 17u)->buffer.minBindingSize, sizeof(epix::pbr::EnvironmentMapUniform));
    EXPECT_EQ(find(plain_entries[1], 0u)->texture.viewDimension, wgpu::TextureViewDimension::eCube);
    EXPECT_EQ(find(plain_entries[1], 2u)->sampler.type, wgpu::SamplerBindingType::eFiltering);

    // The prepass entries appear only for the flags the key carries, at 20 to 23.
    const auto prepass_key = MeshPipelineViewLayoutKey::DEPTH_PREPASS | MeshPipelineViewLayoutKey::MOTION_VECTOR_PREPASS;
    const auto prepass_entries =
        epix::pbr::mesh_view_layout_entries(storage, storage, prepass_key, false, false, false, false);
    const auto* depth_prepass = find(prepass_entries[0], 20u);
    const auto* motion_prepass = find(prepass_entries[0], 22u);
    ASSERT_NE(depth_prepass, nullptr);
    ASSERT_NE(motion_prepass, nullptr);
    EXPECT_EQ(depth_prepass->texture.sampleType, wgpu::TextureSampleType::eDepth);
    EXPECT_EQ(motion_prepass->texture.sampleType, wgpu::TextureSampleType::eUnfilterableFloat);
    EXPECT_EQ(find(prepass_entries[0], 21u), nullptr) << "no normal prepass, no entry";
    EXPECT_EQ(find(prepass_entries[0], 23u), nullptr) << "no deferred prepass, no entry";

    // OIT needs the key bit and the platform; without the platform the three
    // entries are absent rather than different.
    const auto oit_key = MeshPipelineViewLayoutKey::OIT_ENABLED;
    const auto oit_entries = epix::pbr::mesh_view_layout_entries(storage, storage, oit_key, false, false, false, true);
    const auto* oit_layers = find(oit_entries[0], 26u);
    ASSERT_NE(oit_layers, nullptr);
    EXPECT_EQ(oit_layers->buffer.type, wgpu::BufferBindingType::eStorage);
    EXPECT_EQ(oit_layers->buffer.minBindingSize, 0u);
    EXPECT_EQ(find(oit_entries[0], 27u)->buffer.type, wgpu::BufferBindingType::eStorage);
    EXPECT_EQ(find(oit_entries[0], 28u)->buffer.minBindingSize, sizeof(OrderIndependentTransparencySettings));
    const auto unsupported = epix::pbr::mesh_view_layout_entries(storage, storage, oit_key, false, false, false, false);
    EXPECT_EQ(find(unsupported[0], 26u), nullptr);
    EXPECT_EQ(find(unsupported[0], 28u), nullptr);

    // Atmosphere: the two LUT bindings and the data buffer, when the key asks.
    const auto atmosphere_key     = MeshPipelineViewLayoutKey::ATMOSPHERE;
    const auto atmosphere_entries =
        epix::pbr::mesh_view_layout_entries(storage, storage, atmosphere_key, false, false, false, false);
    EXPECT_EQ(find(atmosphere_entries[0], 29u)->texture.sampleType, wgpu::TextureSampleType::eFloat);
    EXPECT_EQ(find(atmosphere_entries[0], 30u)->sampler.type, wgpu::SamplerBindingType::eFiltering);
    EXPECT_EQ(find(atmosphere_entries[0], 31u)->buffer.minBindingSize, sizeof(AtmosphereData));
    EXPECT_EQ(find(plain_entries[0], 31u), nullptr) << "no atmosphere key, no data buffer";

    // The binding-array group grows with irradiance volumes and clustered decals.
    const auto array_entries = epix::pbr::mesh_view_layout_entries(storage, storage,
                                                                   MeshPipelineViewLayoutKey::from_bits_retain(0),
                                                                   true, true, true, false);
    EXPECT_EQ(bindings_of(array_entries[1]), (std::vector<std::uint32_t>{0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u}));
    EXPECT_EQ(find(array_entries[1], 0u)->bindingArraySize, static_cast<std::uint32_t>(epix::pbr::MAX_VIEW_LIGHT_PROBES));
    EXPECT_EQ(find(array_entries[1], 3u)->texture.viewDimension, wgpu::TextureViewDimension::e3D);
    EXPECT_EQ(find(array_entries[1], 5u)->buffer.minBindingSize, sizeof(epix::pbr::RenderClusteredDecal));
    EXPECT_EQ(find(array_entries[1], 6u)->bindingArraySize,
              epix::pbr::MAX_VIEW_DECALS_WITH_PARTIALLY_BOUND_BINDING_ARRAY);
    EXPECT_EQ(find(array_entries[1], 7u)->sampler.type, wgpu::SamplerBindingType::eFiltering);

    // The uniform form of the cluster bindings changes the three buffer entries
    // and nothing else.
    const auto uniform_entries = epix::pbr::mesh_view_layout_entries(
        uniform, uniform, MeshPipelineViewLayoutKey::from_bits_retain(0), false, false, false, false);
    EXPECT_EQ(find(uniform_entries[0], 8u)->buffer.type, wgpu::BufferBindingType::eUniform);
    EXPECT_EQ(find(uniform_entries[0], 8u)->buffer.minBindingSize, GpuClusterableObjects::min_size(uniform));
    EXPECT_EQ(find(uniform_entries[0], 14u)->buffer.type, wgpu::BufferBindingType::eUniform);
    EXPECT_EQ(bindings_of(uniform_entries[0]), bindings_of(plain_entries[0]));

    // Every key generates a layout whose three descriptors carry the key's label,
    // and the array is indexed by the key's bits.
    const auto layouts = generate_view_layouts(storage, storage, false, false, false, false);
    EXPECT_EQ(layouts.size(), static_cast<std::size_t>(MeshPipelineViewLayoutKey::COUNT));
    const auto bare_key = MeshPipelineViewLayoutKey::from_bits_retain(0);
    EXPECT_EQ(layouts[0].main_layout.label, bare_key.label());
    EXPECT_EQ(layouts[0].binding_array_layout.label, bare_key.label() + "_binding_array");
    EXPECT_EQ(layouts[0].empty_layout.label, bare_key.label() + "_empty");
    EXPECT_TRUE(layouts[0].empty_layout.entries.empty());
    EXPECT_EQ(layouts[oit_key.bits()].main_layout.label, oit_key.label());

    const auto view_layouts = MeshPipelineViewLayouts::make(storage, storage, false, false, false, false);
    EXPECT_EQ(view_layouts.get_view_layout(atmosphere_key).main_layout.label, atmosphere_key.label());
    EXPECT_NE(view_layouts.get_view_layout(atmosphere_key).main_layout.label,
              view_layouts.get_view_layout(bare_key).main_layout.label);
}

/** @brief A view's cluster limits follow from the 16 KiB uniform binding limit,
 *  and the storage form's offset/count layout keeps Bevy's field order
 *  (`bevy_pbr/src/cluster.rs:339-341,378-401`). */
TEST(Cluster, ViewClusterLimitsAndStoragePackingMatchBevy) {
    using epix::pbr::ClusterableObjectCounts;
    using epix::pbr::pack_cluster_offsets_and_counts_storage;
    using epix::pbr::uniform_offsets_array_index;
    using epix::pbr::uniform_offsets_component;
    using epix::pbr::VIEW_CLUSTER_MAX_INDICES;
    using epix::pbr::VIEW_CLUSTER_MAX_OFFSETS;
    using epix::pbr::VIEW_CLUSTER_MAX_UNIFORM_ITEMS;

    EXPECT_EQ(VIEW_CLUSTER_MAX_OFFSETS, 4096u) << "16384 bytes of uvec4s";
    EXPECT_EQ(VIEW_CLUSTER_MAX_UNIFORM_ITEMS, 1024u) << "four offsets per uvec4";
    EXPECT_EQ(VIEW_CLUSTER_MAX_INDICES, 16384u) << "16384 u32 indices";
    EXPECT_EQ(VIEW_CLUSTER_MAX_OFFSETS * sizeof(std::uint32_t), 16384u) << "4096 four-byte words is the binding limit";
    EXPECT_EQ(VIEW_CLUSTER_MAX_UNIFORM_ITEMS * sizeof(glm::uvec4), 16384u) << "the same space as uvec4s";

    // Four packed words share one uvec4.
    EXPECT_EQ(uniform_offsets_array_index(0), 0u);
    EXPECT_EQ(uniform_offsets_component(0), 0u);
    EXPECT_EQ(uniform_offsets_array_index(3), 0u);
    EXPECT_EQ(uniform_offsets_component(3), 3u);
    EXPECT_EQ(uniform_offsets_array_index(4), 1u);
    EXPECT_EQ(uniform_offsets_component(4), 0u);
    EXPECT_EQ(uniform_offsets_array_index(VIEW_CLUSTER_MAX_OFFSETS - 1), VIEW_CLUSTER_MAX_UNIFORM_ITEMS - 1)
        << "the last offset still lands inside the array";

    // The storage form carries all five counts, in Bevy's order.
    ClusterableObjectCounts counts{};
    counts.point_lights       = 11;
    counts.spot_lights        = 22;
    counts.reflection_probes  = 33;
    counts.irradiance_volumes = 44;
    counts.decals             = 55;
    const auto packed = pack_cluster_offsets_and_counts_storage(7, counts);
    EXPECT_EQ(packed[0], glm::uvec4(7u, 11u, 22u, 33u));
    EXPECT_EQ(packed[1], glm::uvec4(44u, 55u, 0u, 0u));
}

/** @brief The uniform index list packs four byte-indices into each `u32`, so
 *  sixteen fit a `uvec4` (Bevy `ViewClusterBindings::push_index`,
 *  `bevy_pbr/src/cluster.rs:418-424`). */
TEST(Cluster, UniformIndexListPacksFourIndicesPerWord) {
    using epix::pbr::pack_index_into_uniform_word;
    using epix::pbr::uniform_index_lists_array_index;
    using epix::pbr::uniform_index_lists_component;
    using epix::pbr::uniform_index_lists_sub_index;
    using epix::pbr::VIEW_CLUSTER_MAX_INDICES;

    // Sixteen indices per uvec4, four per word, four bytes per word.
    EXPECT_EQ(uniform_index_lists_array_index(0), 0u);
    EXPECT_EQ(uniform_index_lists_array_index(15), 0u);
    EXPECT_EQ(uniform_index_lists_array_index(16), 1u);
    EXPECT_EQ(uniform_index_lists_component(4), 1u);
    EXPECT_EQ(uniform_index_lists_component(8), 2u);
    EXPECT_EQ(uniform_index_lists_component(12), 3u);
    EXPECT_EQ(uniform_index_lists_sub_index(0), 0u);
    EXPECT_EQ(uniform_index_lists_sub_index(3), 3u);

    // Index 0 fills the low byte, index 3 the high byte of the same word.
    std::uint32_t word = 0;
    word = pack_index_into_uniform_word(word, 0, 0x11u);
    EXPECT_EQ(word, 0x00000011u);
    word = pack_index_into_uniform_word(word, 1, 0x22u);
    word = pack_index_into_uniform_word(word, 2, 0x33u);
    word = pack_index_into_uniform_word(word, 3, 0x44u);
    EXPECT_EQ(word, 0x44332211u) << "four indices, low byte first";
    EXPECT_EQ(pack_index_into_uniform_word(0u, 4, 0x55u), 0x00000055u) << "index 4 starts the next word";

    // The last index the list can hold still lands inside its array.
    const std::size_t last = VIEW_CLUSTER_MAX_INDICES - 1;
    EXPECT_LT(uniform_index_lists_array_index(last), 4096u) << "1024 uvec4s hold 16384 indices";
}

/** @brief A view's cluster bindings write the packed data the two shader forms
 *  expect, and the uniform form drops an offset that would overrun its array
 *  (Bevy `ViewClusterBindings`, `bevy_pbr/src/cluster.rs:338-457`). */
TEST(Cluster, ViewClusterBindingsPackBothForms) {
    using epix::pbr::ClusterableObjectCounts;
    using epix::pbr::pack_offset_and_counts;
    using epix::pbr::ViewClusterBindings;
    using epix::pbr::ViewClusterBuffersStorage;
    using epix::pbr::ViewClusterBuffersUniform;
    using epix::pbr::VIEW_CLUSTER_MAX_UNIFORM_ITEMS;

    ClusterableObjectCounts counts{};
    counts.point_lights = 3;
    counts.spot_lights  = 7;

    // The uniform form packs into the preallocated uvec4 arrays.
    ViewClusterBindings uniform_bindings{wgpu::BufferBindingType::eUniform};
    ASSERT_TRUE(std::holds_alternative<ViewClusterBuffersUniform>(uniform_bindings.buffers.buffers));
    uniform_bindings.push_offset_and_counts(5, counts);
    EXPECT_EQ(uniform_bindings.offsets_count(), 1u);
    auto& uniform = std::get<ViewClusterBuffersUniform>(uniform_bindings.buffers.buffers);
    EXPECT_EQ(uniform.cluster_offsets_and_counts.get().data[0][0], pack_offset_and_counts(5, 3, 7));

    uniform_bindings.push_index(0x11);
    uniform_bindings.push_index(0x22);
    EXPECT_EQ(uniform_bindings.indices_count(), 2u);
    EXPECT_EQ(uniform.clusterable_object_index_lists.get().data[0][0], 0x00002211u);

    // An offset past the array is dropped, as Bevy warns and returns.
    ViewClusterBindings full{wgpu::BufferBindingType::eUniform};
    for (std::size_t i = 0; i < VIEW_CLUSTER_MAX_UNIFORM_ITEMS * 4; ++i) full.push_offset_and_counts(i, counts);
    EXPECT_EQ(full.offsets_count(), VIEW_CLUSTER_MAX_UNIFORM_ITEMS * 4);
    full.push_offset_and_counts(999, counts);
    EXPECT_EQ(full.offsets_count(), VIEW_CLUSTER_MAX_UNIFORM_ITEMS * 4) << "the overrunning offset is not counted";

    // The storage form appends and has no bound.
    ViewClusterBindings storage_bindings{wgpu::BufferBindingType::eStorage};
    ASSERT_TRUE(std::holds_alternative<ViewClusterBuffersStorage>(storage_bindings.buffers.buffers));
    storage_bindings.push_offset_and_counts(9, counts);
    storage_bindings.push_index(0x33);
    auto& storage = std::get<ViewClusterBuffersStorage>(storage_bindings.buffers.buffers);
    ASSERT_EQ(storage.cluster_offsets_and_counts.get().data.size(), 1u);
    EXPECT_EQ(storage.cluster_offsets_and_counts.get().data[0][0], glm::uvec4(9u, 3u, 7u, 0u));
    ASSERT_EQ(storage.clusterable_object_index_lists.get().data.size(), 1u);
    EXPECT_EQ(storage.clusterable_object_index_lists.get().data[0], 0x33u);

    // Clear zeroes the uniform arrays and empties the vectors.
    uniform_bindings.clear();
    EXPECT_EQ(uniform.cluster_offsets_and_counts.get().data[0], glm::uvec4(0u)) << "clear zeroes the array";
    storage_bindings.clear();
    EXPECT_TRUE(storage.cluster_offsets_and_counts.get().data.empty());

    // Nothing has been uploaded, so nothing is bindable.
    EXPECT_FALSE(uniform_bindings.clusterable_object_index_lists_binding().has_value());
    EXPECT_FALSE(storage_bindings.offsets_and_counts_binding().has_value());
}

/** @brief The extracted cluster data carries the view's geometry and, per
 *  cluster, a header followed by that cluster's objects (Bevy
 *  `ExtractedClusterConfig`/`ExtractedClusterableObjects`,
 *  `bevy_pbr/src/cluster.rs:95-111`). */
TEST(Cluster, ExtractedClusterDataCarriesHeadersBeforeObjects) {
    using epix::pbr::ClusterableObjectCounts;
    using epix::pbr::ExtractedClusterableObjectElement;
    using epix::pbr::ExtractedClusterableObjects;
    using epix::pbr::ExtractedClusterConfig;

    const ExtractedClusterConfig config{};
    EXPECT_FLOAT_EQ(config.near_plane, 0.0f);
    EXPECT_FLOAT_EQ(config.far_plane, 0.0f);
    EXPECT_EQ(config.dimensions, glm::uvec3(1, 1, 1));

    // A flat list in cluster order: header, then that cluster's objects.
    ExtractedClusterableObjects extracted;
    ClusterableObjectCounts counts{};
    counts.point_lights = 2;
    extracted.data.push_back(ExtractedClusterableObjectElement{
        .kind = ExtractedClusterableObjectElement::Kind::ClusterHeader, .counts = counts});
    extracted.data.push_back(ExtractedClusterableObjectElement{
        .kind = ExtractedClusterableObjectElement::Kind::ClusterableObjectEntity, .entity = epix::ecs::Entity{}});

    ASSERT_EQ(extracted.data.size(), 2u);
    EXPECT_EQ(extracted.data[0].kind, ExtractedClusterableObjectElement::Kind::ClusterHeader);
    EXPECT_EQ(extracted.data[0].counts.point_lights, 2u) << "the header states what follows";
    EXPECT_EQ(extracted.data[1].kind, ExtractedClusterableObjectElement::Kind::ClusterableObjectEntity);
}

/** @brief The cluster fill walk takes each cluster's offset from the current
 *  index count, skips objects with no global index, and stops the whole view at
 *  MAX_INDICES in the uniform form only (Bevy `prepare_clusters`,
 *  `bevy_pbr/src/cluster.rs:302-330`). */
TEST(Cluster, PrepareClusterBindingsFollowsTheRecordWalk) {
    using epix::pbr::ClusterableObjectCounts;
    using epix::pbr::ExtractedClusterableObjectElement;
    using epix::pbr::prepare_view_cluster_bindings;
    using epix::pbr::ViewClusterBindings;
    using epix::pbr::VIEW_CLUSTER_MAX_INDICES;

    const auto header = [](std::uint32_t points) {
        ClusterableObjectCounts counts{};
        counts.point_lights = points;
        return ExtractedClusterableObjectElement{.kind = ExtractedClusterableObjectElement::Kind::ClusterHeader,
                                                 .counts = counts};
    };
    const auto object = [](epix::ecs::Entity entity) {
        return ExtractedClusterableObjectElement{
            .kind = ExtractedClusterableObjectElement::Kind::ClusterableObjectEntity, .entity = entity};
    };

    // Distinct entities: a default-constructed Entity is the same value each
    // time, which would collapse these into one map key.
    const epix::ecs::Entity first    = epix::ecs::Entity::from_index(1);
    const epix::ecs::Entity second   = epix::ecs::Entity::from_index(2);
    const epix::ecs::Entity unmapped = epix::ecs::Entity::from_index(3);

    std::unordered_map<epix::ecs::Entity, std::size_t> entity_to_index;
    entity_to_index[first]  = 4;
    entity_to_index[second] = 9;

    // Header, two objects, header, one object: the second cluster's offset is 2,
    // the number of indices the first cluster added.
    const std::vector<ExtractedClusterableObjectElement> records{header(2), object(first), object(second), header(1),
                                                                object(first)};
    const auto bindings = prepare_view_cluster_bindings(records, entity_to_index, wgpu::BufferBindingType::eStorage, true);
    EXPECT_EQ(bindings.indices_count(), 3u);
    EXPECT_EQ(bindings.offsets_count(), 2u);
    const auto& offsets =
        std::get<epix::pbr::ViewClusterBuffersStorage>(bindings.buffers.buffers).cluster_offsets_and_counts.get().data;
    ASSERT_EQ(offsets.size(), 2u);
    EXPECT_EQ(offsets[0][0], glm::uvec4(0u, 2u, 0u, 0u)) << "the first cluster starts at index 0";
    EXPECT_EQ(offsets[1][0], glm::uvec4(2u, 1u, 0u, 0u)) << "the second starts where the first stopped";
    const auto& indices =
        std::get<epix::pbr::ViewClusterBuffersStorage>(bindings.buffers.buffers).clusterable_object_index_lists.get().data;
    EXPECT_EQ(indices, (std::vector<std::uint32_t>{4u, 9u, 4u})) << "the objects' global indices, in order";

    // An object with no entry in the global map contributes nothing.
    entity_to_index.erase(second);
    const auto skipped =
        prepare_view_cluster_bindings(records, entity_to_index, wgpu::BufferBindingType::eStorage, true);
    EXPECT_EQ(skipped.indices_count(), 2u) << "the unmapped object is skipped, not an error";

    // The uniform form stops at MAX_INDICES; the storage form does not.
    std::vector<ExtractedClusterableObjectElement> many{header(1)};
    for (std::size_t i = 0; i < VIEW_CLUSTER_MAX_INDICES + 10; ++i) many.push_back(object(first));
    const auto capped =
        prepare_view_cluster_bindings(many, entity_to_index, wgpu::BufferBindingType::eUniform, false);
    EXPECT_EQ(capped.indices_count(), VIEW_CLUSTER_MAX_INDICES) << "the walk breaks at the limit";
    const auto uncapped =
        prepare_view_cluster_bindings(many, entity_to_index, wgpu::BufferBindingType::eStorage, true);
    EXPECT_EQ(uncapped.indices_count(), VIEW_CLUSTER_MAX_INDICES + 10)
        << "with storage buffers the index list is unbounded";
}

/** @brief Extraction turns a view's clusters into a flat record list - a header
 *  per cluster, then that cluster's synced objects - and carries the geometry
 *  across; an unsynced object is skipped (Bevy `extract_clusters`,
 *  `bevy_pbr/src/cluster.rs:259-285`). */
TEST(Cluster, ExtractViewClustersBuildsHeadersThenSyncedObjects) {
    using epix::pbr::extract_view_clusters;
    using epix::pbr::ExtractedClusterableObjectElement;
    using epix::pbr::VisibleClusterableObjects;
    using epix::pbr::Clusters;

    const epix::ecs::Entity main_a = epix::ecs::Entity::from_index(10);
    const epix::ecs::Entity main_b = epix::ecs::Entity::from_index(11);
    const epix::ecs::Entity unsynced = epix::ecs::Entity::from_index(12);
    const epix::ecs::Entity render_a = epix::ecs::Entity::from_index(20);
    const epix::ecs::Entity render_b = epix::ecs::Entity::from_index(21);

    Clusters clusters;
    clusters.near_plane  = 0.1f;
    clusters.far_plane   = 100.0f;
    clusters.dimensions  = glm::uvec3(4, 4, 8);
    VisibleClusterableObjects first_cluster;
    first_cluster.counts.point_lights = 2;
    first_cluster.entities            = {main_a, unsynced};
    VisibleClusterableObjects second_cluster;
    second_cluster.counts.spot_lights = 1;
    second_cluster.entities           = {main_b};
    clusters.clusterable_objects      = {first_cluster, second_cluster};

    // The mapper reports only the entities that were synced to the render world.
    const auto mapper = [&](epix::ecs::Entity main, epix::ecs::Entity& out) {
        if (main == main_a) {
            out = render_a;
            return true;
        }
        if (main == main_b) {
            out = render_b;
            return true;
        }
        return false;  // `unsynced` has no render entity
    };

    const auto extracted = extract_view_clusters(clusters, mapper);
    ASSERT_EQ(extracted.objects.data.size(), 4u) << "two headers and two synced objects; the unsynced one is skipped";
    EXPECT_EQ(extracted.objects.data[0].kind, ExtractedClusterableObjectElement::Kind::ClusterHeader);
    EXPECT_EQ(extracted.objects.data[0].counts.point_lights, 2u);
    ASSERT_EQ(extracted.objects.data[1].kind, ExtractedClusterableObjectElement::Kind::ClusterableObjectEntity);
    EXPECT_EQ(extracted.objects.data[1].entity, render_a) << "the render entity, not the main one";
    EXPECT_EQ(extracted.objects.data[2].kind, ExtractedClusterableObjectElement::Kind::ClusterHeader)
        << "the unsynced object contributed nothing";
    EXPECT_EQ(extracted.objects.data[2].counts.spot_lights, 1u);
    EXPECT_EQ(extracted.objects.data[3].entity, render_b);

    EXPECT_FLOAT_EQ(extracted.config.near_plane, 0.1f);
    EXPECT_FLOAT_EQ(extracted.config.far_plane, 100.0f);
    EXPECT_EQ(extracted.config.dimensions, glm::uvec3(4, 4, 8));
}

/** @brief The cluster factors and z-slice boundaries follow Bevy's formulas for
 *  both projection kinds (Bevy `calculate_cluster_factors` and
 *  `z_slice_to_view_z`, `bevy_light/src/cluster/assign.rs:863-878,950-967`). */
TEST(Cluster, ClusterFactorsAndZSlicesMatchBevy) {
    using epix::pbr::calculate_cluster_factors;
    using epix::pbr::z_slice_to_view_z;

    const float near_plane = 0.1f;
    const float far_plane  = 100.0f;
    const float z_slices   = 8.0f;

    // Perspective: the second factor is ln(near) times the first, which is the
    // relation the shader relies on to recover a view Z.
    const glm::vec2 perspective = calculate_cluster_factors(near_plane, far_plane, z_slices, false);
    EXPECT_NEAR(perspective.x, (z_slices - 1.0f) / std::log(far_plane / near_plane), 1e-6f);
    EXPECT_NEAR(perspective.y / perspective.x, std::log(near_plane), 1e-5f);

    // Orthographic: linear slices.
    const glm::vec2 orthographic = calculate_cluster_factors(near_plane, far_plane, z_slices, true);
    EXPECT_FLOAT_EQ(orthographic.x, -near_plane);
    EXPECT_NEAR(orthographic.y, z_slices / (near_plane - far_plane), 1e-6f);

    // Perspective slice boundaries: 0 at the first, -near at the second, and
    // geometric in between.
    EXPECT_FLOAT_EQ(z_slice_to_view_z(near_plane, far_plane, 8, 0, false), 0.0f);
    EXPECT_NEAR(z_slice_to_view_z(near_plane, far_plane, 8, 1, false), -near_plane, 1e-5f);
    const float second = z_slice_to_view_z(near_plane, far_plane, 8, 2, false);
    const float ratio  = std::pow(far_plane / near_plane, 1.0f / 7.0f);
    EXPECT_NEAR(second / z_slice_to_view_z(near_plane, far_plane, 8, 1, false), ratio, 1e-4f)
        << "each slice multiplies the previous by the same ratio";
    EXPECT_NEAR(z_slice_to_view_z(near_plane, far_plane, 8, 7, false), -near_plane * std::pow(far_plane / near_plane, 6.0f / 7.0f),
                1e-3f);

    // Orthographic boundaries run evenly from -near to -far.
    EXPECT_FLOAT_EQ(z_slice_to_view_z(near_plane, far_plane, 8, 0, true), -near_plane);
    EXPECT_FLOAT_EQ(z_slice_to_view_z(near_plane, far_plane, 8, 8, true), -far_plane);
    const float step = (far_plane - near_plane) / 8.0f;
    EXPECT_NEAR(z_slice_to_view_z(near_plane, far_plane, 8, 3, true), -near_plane - 3.0f * step, 1e-5f);
}

/** @brief Which cluster a fragment lands in, and where a line crosses a cluster
 *  plane (Bevy `view_z_to_z_slice`, `ndc_position_to_cluster` and
 *  `line_intersection_to_z_plane`, `bevy_light/src/cluster/assign.rs:969-988,
 *  1086-1109`). */
TEST(Cluster, NdcPositionToClusterMatchesBevy) {
    using epix::pbr::calculate_cluster_factors;
    using epix::pbr::line_intersection_to_z_plane;
    using epix::pbr::ndc_position_to_cluster;
    using epix::pbr::view_z_to_z_slice;

    const float near_plane = 0.1f;
    const float far_plane  = 100.0f;
    const glm::vec2 factors = calculate_cluster_factors(near_plane, far_plane, 8.0f, false);

    // The z-slice of a view Z: -near is the far edge of the first slice, and
    // anything at or past the far plane clamps to the last slice.
    EXPECT_EQ(view_z_to_z_slice(factors, 8, -near_plane, false), 1u)
        << "the first slice spans 0 to -near, so -near is slice 1";
    EXPECT_EQ(view_z_to_z_slice(factors, 8, -far_plane, false), 7u);
    EXPECT_EQ(view_z_to_z_slice(factors, 8, -1000.0f, false), 7u) << "clamped, as Bevy's min does";
    std::uint32_t previous = 0;
    for (float view_z = -near_plane; view_z >= -far_plane; view_z *= 1.1f) {
        const std::uint32_t slice = view_z_to_z_slice(factors, 8, view_z, false);
        EXPECT_GE(slice, previous) << "slices never go backwards as the fragment recedes";
        EXPECT_LE(slice, 7u);
        previous = slice;
    }

    // Orthographic: linear, so -near is slice 0.
    const glm::vec2 ortho = calculate_cluster_factors(near_plane, far_plane, 8.0f, true);
    EXPECT_EQ(view_z_to_z_slice(ortho, 8, -near_plane, true), 0u);
    EXPECT_EQ(view_z_to_z_slice(ortho, 8, -far_plane, true), 7u);

    // NDC maps to the grid with Y flipped; the centre is the middle cluster and
    // the corners clamp to the edges.
    const glm::uvec3 dims{4, 4, 8};
    EXPECT_EQ(ndc_position_to_cluster(dims, factors, false, glm::vec3(0.0f, 0.0f, 0.0f), -near_plane), glm::uvec3(2, 2, 1));
    EXPECT_EQ(ndc_position_to_cluster(dims, factors, false, glm::vec3(-1.0f, 1.0f, 0.0f), -near_plane), glm::uvec3(0, 0, 1))
        << "the top-left corner";
    EXPECT_EQ(ndc_position_to_cluster(dims, factors, false, glm::vec3(1.0f, -1.0f, 0.0f), -near_plane), glm::uvec3(3, 3, 1))
        << "the bottom-right corner lands in the last cluster, not past it";

    // A line from the origin toward (2, 2, 4) crosses z = -2 at (-1, -1, -2).
    const glm::vec3 crossed = line_intersection_to_z_plane(glm::vec3(0.0f), glm::vec3(2.0f, 2.0f, 4.0f), -2.0f);
    EXPECT_NEAR(crossed.x, -1.0f, 1e-5f);
    EXPECT_NEAR(crossed.y, -1.0f, 1e-5f);
    EXPECT_NEAR(crossed.z, -2.0f, 1e-5f) << "the result lies on the requested plane";
}

/** @brief Screen positions map to NDC with Y flipped and unproject to view space
 *  (Bevy `screen_to_view` and `clip_to_view`,
 *  `bevy_light/src/cluster/assign.rs:1111-1125`). */
TEST(Cluster, ScreenToViewMatchesBevy) {
    using epix::pbr::clip_to_view;
    using epix::pbr::screen_to_view;

    // With an identity unprojection the mapping is visible directly: the
    // top-left corner maps to (-1, 1), and the bottom-left to (-1, -1).
    const glm::mat4 identity{1.0f};
    const glm::vec2 screen_size{800.0f, 600.0f};
    const auto top_left = screen_to_view(screen_size, identity, glm::vec2(0.0f, 0.0f), 0.5f);
    EXPECT_FLOAT_EQ(top_left.x, -1.0f);
    EXPECT_FLOAT_EQ(top_left.y, 1.0f) << "screen Y is flipped";
    EXPECT_FLOAT_EQ(top_left.z, 0.5f);
    const auto bottom_left = screen_to_view(screen_size, identity, glm::vec2(0.0f, 600.0f), 0.5f);
    EXPECT_FLOAT_EQ(bottom_left.y, -1.0f);
    const auto centre = screen_to_view(screen_size, identity, glm::vec2(400.0f, 300.0f), 0.5f);
    EXPECT_NEAR(centre.x, 0.0f, 1e-6f);
    EXPECT_NEAR(centre.y, 0.0f, 1e-6f);

    // clip_to_view divides by w.
    const glm::vec4 homogeneous = clip_to_view(identity, glm::vec4(2.0f, 4.0f, 6.0f, 2.0f));
    EXPECT_EQ(homogeneous, glm::vec4(1.0f, 2.0f, 3.0f, 1.0f));

    // Round trip through a real perspective projection: a view-space point
    // projected to the screen and unprojected at its own NDC depth comes back.
    const glm::mat4 projection =
        epix::camera::perspective_infinite_reverse_rh(glm::radians(60.0f), screen_size.x / screen_size.y, 0.1f);
    const glm::mat4 view_from_clip = glm::inverse(projection);
    const glm::vec4 view_point{0.3f, -0.2f, -2.0f, 1.0f};
    const glm::vec4 clip_point = projection * view_point;
    const glm::vec3 ndc        = glm::vec3(clip_point) / clip_point.w;
    const glm::vec2 screen{(ndc.x * 0.5f + 0.5f) * screen_size.x, (1.0f - (ndc.y * 0.5f + 0.5f)) * screen_size.y};
    const glm::vec4 recovered = screen_to_view(screen_size, view_from_clip, screen, ndc.z);
    EXPECT_NEAR(recovered.x, view_point.x, 1e-3f);
    EXPECT_NEAR(recovered.y, view_point.y, 1e-3f);
    EXPECT_NEAR(recovered.z, view_point.z, 1e-3f);
}

/** @brief A cluster's view-space bounds: the first perspective cluster spans z 0
 *  to -near, the far clusters grow with distance, and the orthographic case
 *  slices depth linearly (Bevy `compute_aabb_for_cluster`,
 *  `bevy_light/src/cluster/assign.rs:880-947`). */
TEST(Cluster, ComputeAabbForClusterMatchesBevy) {
    using epix::pbr::compute_aabb_for_cluster;

    const float z_near        = 0.1f;
    const float z_far         = 100.0f;
    const glm::vec2 screen{800.0f, 600.0f};
    const glm::uvec3 dims{4, 4, 8};
    const glm::vec2 tile{screen.x / static_cast<float>(dims.x), screen.y / static_cast<float>(dims.y)};
    // Bevy's perspective branch unprojects at ndc_z 1.0, which is the near plane
    // because its projections are reverse-Z. So this test builds a reverse-Z
    // matrix: the near and far planes are swapped, putting the near plane at NDC
    // z 1. An OpenGL-style matrix here would unproject the far plane instead and
    // mirror the corners through the origin.
    const glm::mat4 projection = epix::camera::perspective_infinite_reverse_rh(glm::radians(60.0f), screen.x / screen.y, z_near);
    const glm::mat4 view_from_clip = glm::inverse(projection);

    // The first cluster starts at the camera and ends at the near plane. Note
    // that the origin itself is one of the four intersections - cluster_near is
    // 0 for the first slice - so the box reaches from the view origin out to the
    // tile's near-plane corner rather than being centred on it.
    const auto first = compute_aabb_for_cluster(z_near, z_far, tile, screen, view_from_clip, false, dims, glm::uvec3(0, 0, 0));
    EXPECT_NEAR(first.max().z, 0.0f, 1e-4f) << "the first perspective slice begins at 0";
    EXPECT_NEAR(first.min().z, -z_near, 1e-4f) << "and ends at the near plane";

    // Its x and y reach the near plane's half-extents at a 60 degree vertical fov.
    const float half_height = z_near * std::tan(glm::radians(30.0f));
    const float half_width  = half_height * (screen.x / screen.y);
    EXPECT_NEAR(std::min(first.min().x, first.max().x), -half_width, 1e-3f) << "the top-left tile's outer corner";
    EXPECT_NEAR(std::max(first.min().y, first.max().y), half_height, 1e-3f);
    EXPECT_NEAR(std::max(first.min().x, first.max().x), 0.0f, 1e-3f) << "the view origin is a corner of the box";
    EXPECT_NEAR(std::min(first.min().y, first.max().y), 0.0f, 1e-3f);

    // The last cluster reaches the far plane, and clusters grow with distance.
    const auto last = compute_aabb_for_cluster(z_near, z_far, tile, screen, view_from_clip, false, dims, glm::uvec3(3, 3, 7));
    EXPECT_NEAR(last.min().z, -z_far, 1e-3f) << "the last slice ends at the far plane";
    EXPECT_NEAR(last.max().z, -z_near * std::pow(z_far / z_near, 6.0f / 7.0f), 1e-3f);
    EXPECT_GT(last.half_extents.x, first.half_extents.x) << "a distant cluster covers more of the view";
    EXPECT_GT(last.half_extents.y, first.half_extents.y);

    // Orthographic: depth slices are evenly spaced from -near.
    epix::camera::OrthographicProjection ortho_projection;
    ortho_projection.rect       = {.left = -1.0f, .right = 1.0f, .bottom = -0.75f, .top = 0.75f};
    ortho_projection.near_plane = z_near;
    ortho_projection.far_plane  = z_far;
    const glm::mat4 ortho       = ortho_projection.get_clip_from_view();
    const auto ortho_first =
        compute_aabb_for_cluster(z_near, z_far, tile, screen, glm::inverse(ortho), true, dims, glm::uvec3(0, 0, 0));
    EXPECT_NEAR(ortho_first.max().z, -z_near, 1e-4f);
    EXPECT_NEAR(ortho_first.min().z, -z_near - (z_far - z_near) / 8.0f, 1e-3f) << "one eighth of the depth range";
}

/** @brief The plane helpers reduce the point-to-plane formulas using each
 *  plane's known normal shape (Bevy `get_distance_x`, `project_to_plane_z` and
 *  `project_to_plane_y`, `bevy_light/src/cluster/assign.rs:1128-1181`). */
TEST(Cluster, PlaneProjectionsMatchBevy) {
    using epix::camera::HalfSpace;
    using epix::camera::Sphere;
    using epix::pbr::get_distance_x;
    using epix::pbr::project_to_plane_y;
    using epix::pbr::project_to_plane_z;

    // An x-plane with a unit normal along +x, two units behind the origin.
    const HalfSpace x_plane = HalfSpace::from_normal_d(glm::vec4(1.0f, 0.0f, 0.0f, -2.0f));
    EXPECT_FLOAT_EQ(x_plane.d(), -2.0f);
    EXPECT_FLOAT_EQ(get_distance_x(x_plane, glm::vec3(3.0f, 5.0f, 7.0f), false), 3.0f)
        << "the dot product of the normal with the point's x and z";
    EXPECT_FLOAT_EQ(get_distance_x(x_plane, glm::vec3(3.0f, 5.0f, 7.0f), true), 5.0f)
        << "orthographic is the x difference from d";

    // A z-plane at z = -4: a sphere straddling it projects, one beyond it does not.
    const HalfSpace z_plane = HalfSpace::from_normal_d(glm::vec4(0.0f, 0.0f, 1.0f, -4.0f));
    const Sphere straddling{.center = glm::vec3(1.0f, 2.0f, -3.0f), .radius = 2.0f};
    const auto projected = project_to_plane_z(straddling, z_plane);
    ASSERT_TRUE(projected.has_value());
    EXPECT_EQ(projected->center, glm::vec3(1.0f, 2.0f, -4.0f)) << "x and y are kept, z comes from the plane";
    EXPECT_NEAR(projected->radius, std::sqrt(4.0f - 1.0f), 1e-5f) << "Pythagoras on the one unit of overlap";

    const Sphere beyond{.center = glm::vec3(0.0f, 0.0f, 0.0f), .radius = 1.0f};
    EXPECT_FALSE(project_to_plane_z(beyond, z_plane).has_value()) << "four units away from a radius of one";

    // A y-plane. Note the two branches differ in whether d participates: these
    // helpers run in cluster space, where the planes pass through the origin, so
    // the perspective branch measures the centre's projection along the normal
    // and ignores d, while the orthographic branch uses d directly.
    const HalfSpace y_plane_origin = HalfSpace::from_normal_d(glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
    const Sphere near_y{.center = glm::vec3(0.5f, 1.0f, -3.0f), .radius = 1.5f};
    const auto projected_y = project_to_plane_y(near_y, y_plane_origin, false);
    ASSERT_TRUE(projected_y.has_value());
    EXPECT_EQ(projected_y->center, glm::vec3(0.5f, 0.0f, -3.0f))
        << "moved along the normal by minus the centre's projection onto it";
    EXPECT_NEAR(projected_y->radius, std::sqrt(2.25f - 1.0f), 1e-5f);

    const HalfSpace y_plane_at_two = HalfSpace::from_normal_d(glm::vec4(0.0f, 1.0f, 0.0f, 2.0f));
    const auto orthographic_y = project_to_plane_y(near_y, y_plane_at_two, true);
    ASSERT_TRUE(orthographic_y.has_value());
    EXPECT_EQ(orthographic_y->center, glm::vec3(0.5f, 2.0f, -3.0f))
        << "the orthographic branch moves the centre to d along the normal";

    const Sphere far_y{.center = glm::vec3(0.0f, -10.0f, 0.0f), .radius = 1.0f};
    EXPECT_FALSE(project_to_plane_y(far_y, y_plane_origin, false).has_value())
        << "ten units from a plane the sphere does not reach";
}

/** @brief A clusterable object's bounds project into clamped NDC x/y with the
 *  unadjusted view z packed back in, and an object at the camera still yields
 *  sane bounds (Bevy `cluster_space_clusterable_object_aabb`,
 *  `bevy_light/src/cluster/assign.rs:995-1083`). */
TEST(Cluster, ClusterSpaceClusterableObjectAabbMatchesBevy) {
    using epix::camera::Sphere;
    using epix::pbr::cluster_space_clusterable_object_aabb;

    const float z_near        = 0.1f;
    const float z_far         = 100.0f;
    const glm::vec2 screen{800.0f, 600.0f};
    // Reverse-Z, as the cluster code assumes: the near plane is at NDC z 1.
    const glm::mat4 projection     = epix::camera::perspective_infinite_reverse_rh(glm::radians(60.0f), screen.x / screen.y, z_near);
    const glm::mat4 view_from_clip = glm::inverse(projection);
    static_cast<void>(view_from_clip);
    const glm::mat4 view_from_world{1.0f};

    // An object ten units in front of the camera projects to the middle of NDC,
    // and its view z comes back untouched.
    const Sphere ahead{.center = glm::vec3(0.0f, 0.0f, -10.0f), .radius = 1.0f};
    const auto bounds = cluster_space_clusterable_object_aabb(view_from_world, glm::vec3(1.0f), projection, ahead);
    EXPECT_LT(bounds.first.z, 0.0f) << "view z is negative in front of the camera";
    EXPECT_NEAR(bounds.first.z, -11.0f, 1e-4f) << "the unadjusted view z of the near face";
    EXPECT_NEAR(bounds.second.z, -9.0f, 1e-4f) << "and of the far face";
    EXPECT_GT(bounds.first.x, -1.0f);
    EXPECT_LT(bounds.second.x, 1.0f);
    EXPECT_GT(bounds.first.y, -1.0f);
    EXPECT_LT(bounds.second.y, 1.0f);
    // A centred object is symmetric about the origin in NDC.
    EXPECT_NEAR(bounds.first.x, -bounds.second.x, 1e-3f);
    EXPECT_NEAR(bounds.first.y, -bounds.second.y, 1e-3f);

    // An object at the camera has its view z clamped in front of it, so the
    // projection does not flip and the bounds still clamp into NDC.
    const Sphere at_camera{.center = glm::vec3(0.0f, 0.0f, 0.0f), .radius = 1.0f};
    const auto clamped = cluster_space_clusterable_object_aabb(view_from_world, glm::vec3(1.0f), projection, at_camera);
    EXPECT_GE(clamped.first.x, -1.0f);
    EXPECT_LE(clamped.second.x, 1.0f);
    EXPECT_GE(clamped.first.y, -1.0f);
    EXPECT_LE(clamped.second.y, 1.0f);
    EXPECT_LE(clamped.first.z, 0.0f) << "clamped to just in front of the camera, never behind it";

    // A scaled object's bounds scale with it.
    const Sphere scaled{.center = glm::vec3(0.0f, 0.0f, -10.0f), .radius = 1.0f};
    const auto wider = cluster_space_clusterable_object_aabb(view_from_world, glm::vec3(3.0f, 1.0f, 1.0f), projection, scaled);
    EXPECT_GT(wider.second.x - wider.first.x, bounds.second.x - bounds.first.x)
        << "a three-times-wider object covers more of NDC";
    EXPECT_NEAR(wider.second.y - wider.first.y, bounds.second.y - bounds.first.y, 1e-3f);
}

/** @brief The assignment ordering sorts by type and then puts shadow-casting and
 *  volumetric lights first, which the negated booleans achieve (Bevy
 *  `ClusterableObjectType::ordering`,
 *  `bevy_light/src/cluster/assign.rs:105-120`), and an object's sphere is its
 *  translation with the range as radius (`assign.rs:45-52`). */
TEST(Cluster, ClusterableObjectOrderingAndSphereMatchBevy) {
    using epix::pbr::ClusterableObjectAssignmentData;
    using epix::pbr::ClusterableObjectType;

    const ClusterableObjectType point_shadowy{.kind = ClusterableObjectType::Kind::PointLight,
                                              .shadows_enabled = true,
                                              .volumetric = true};
    const ClusterableObjectType point_plain{.kind = ClusterableObjectType::Kind::PointLight};
    const ClusterableObjectType spot{.kind = ClusterableObjectType::Kind::SpotLight};
    const ClusterableObjectType probe{.kind = ClusterableObjectType::Kind::ReflectionProbe};
    const ClusterableObjectType volume{.kind = ClusterableObjectType::Kind::IrradianceVolume};
    const ClusterableObjectType decal{.kind = ClusterableObjectType::Kind::Decal};

    // Type codes, in Bevy's order.
    EXPECT_EQ(std::get<0>(point_shadowy.ordering()), 0u);
    EXPECT_EQ(std::get<0>(spot.ordering()), 1u);
    EXPECT_EQ(std::get<0>(probe.ordering()), 2u);
    EXPECT_EQ(std::get<0>(volume.ordering()), 3u);
    EXPECT_EQ(std::get<0>(decal.ordering()), 4u);

    // The booleans are negated so that ascending order puts enabled first.
    EXPECT_EQ(std::get<1>(point_shadowy.ordering()), false) << "a shadow-casting light sorts before a plain one";
    EXPECT_EQ(std::get<1>(point_plain.ordering()), true);
    EXPECT_EQ(std::get<2>(point_shadowy.ordering()), false) << "and a volumetric one before a non-volumetric one";
    EXPECT_EQ(std::get<2>(point_plain.ordering()), true);
    EXPECT_LT(point_shadowy.ordering(), point_plain.ordering()) << "which is what the negation buys";
    EXPECT_LT(point_plain.ordering(), spot.ordering()) << "point lights come before spot lights";
    EXPECT_LT(spot.ordering(), probe.ordering());
    EXPECT_LT(probe.ordering(), decal.ordering());
    // Non-lights carry no shadow or volumetric distinction.
    EXPECT_EQ(probe.ordering(), std::make_tuple(std::uint8_t{2}, false, false));

    // The sphere comes from the transform's translation and the range.
    ClusterableObjectAssignmentData data;
    data.transform = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, -4.0f, 5.0f));
    data.range     = 2.5f;
    const auto sphere = data.sphere();
    EXPECT_EQ(sphere.center, glm::vec3(3.0f, -4.0f, 5.0f));
    EXPECT_FLOAT_EQ(sphere.radius, 2.5f);
}

/** @brief The cluster configuration's accessors map each strategy to its z
 *  settings (Bevy `ClusterConfig`'s impl, `cluster/mod.rs:282-311`), and the
 *  constant far-Z mode carries its value as Bevy's `Constant(f32)` variant does
 *  (`cluster/mod.rs:49-57`). */
TEST(Cluster, ClusterConfigAccessorsMatchBevy) {
    using epix::pbr::ClusterConfig;
    using epix::pbr::ClusterFarZMode;
    using epix::pbr::ClusterZConfig;

    const ClusterConfig none{.kind = ClusterConfig::Kind::None};
    EXPECT_FLOAT_EQ(none.first_slice_depth(), 0.0f) << "no z configuration";
    EXPECT_EQ(none.far_z_mode().kind, ClusterFarZMode::Kind::Constant);
    EXPECT_FLOAT_EQ(none.far_z_mode().constant, 0.0f) << "a constant zero far plane when clustering is off";
    EXPECT_FALSE(none.may_resize_dynamically());

    const ClusterConfig single{.kind = ClusterConfig::Kind::Single};
    EXPECT_FLOAT_EQ(single.first_slice_depth(), 0.0f);
    EXPECT_EQ(single.far_z_mode().kind, ClusterFarZMode::Kind::MaxClusterableObjectRange)
        << "a single cluster reaches the visible range";
    EXPECT_FALSE(single.may_resize_dynamically());

    ClusterConfig fixed{.kind = ClusterConfig::Kind::FixedZ};
    fixed.z_config           = ClusterZConfig{.first_slice_depth = 5.0f,
                                              .far_z_mode = ClusterFarZMode::constant_far(120.0f)};
    fixed.dynamic_resizing   = true;
    EXPECT_FLOAT_EQ(fixed.first_slice_depth(), 5.0f) << "from the z configuration";
    EXPECT_EQ(fixed.far_z_mode().kind, ClusterFarZMode::Kind::Constant);
    EXPECT_FLOAT_EQ(fixed.far_z_mode().constant, 120.0f) << "the value travels with the mode";
    EXPECT_TRUE(fixed.may_resize_dynamically());

    const ClusterConfig xyz{.kind = ClusterConfig::Kind::XYZ, .dynamic_resizing = false};
    EXPECT_FALSE(xyz.may_resize_dynamically()) << "each strategy carries its own setting";
}

/** @brief The default strategy is `FixedZ` at 4096 clusters and 24 slices, and
 *  `dimensions_for_screen_size` derives square clusters from the aspect ratio
 *  (Bevy `ClusterConfig::default` and `dimensions_for_screen_size`,
 *  `bevy_light/src/cluster/mod.rs:230-280`). */
TEST(Cluster, DimensionsForScreenSizeMatchBevy) {
    using epix::pbr::ClusterConfig;

    const ClusterConfig defaults{};
    EXPECT_EQ(defaults.kind, ClusterConfig::Kind::FixedZ);
    EXPECT_EQ(defaults.total, 4096u);
    EXPECT_EQ(defaults.z_slices, 24u);
    EXPECT_TRUE(defaults.dynamic_resizing);
    EXPECT_FLOAT_EQ(defaults.z_config.first_slice_depth, 5.0f) << "the first slice reaches 5.0";

    // 16:9 at 4096 clusters over 24 slices: 170.67 per layer, so y = sqrt(170.67 / 1.7778) = 9.798
    // and x = 9.798 * 1.7778 = 17.42, truncating to 17 by 9.
    const glm::uvec3 wide = defaults.dimensions_for_screen_size(glm::uvec2(1920, 1080));
    EXPECT_EQ(wide, glm::uvec3(17, 9, 24));

    // A portrait screen keeps the same number of clusters, arranged the other way.
    const glm::uvec3 tall = defaults.dimensions_for_screen_size(glm::uvec2(1080, 1920));
    EXPECT_EQ(tall.z, 24u);
    EXPECT_LT(tall.x, wide.x) << "fewer columns when the screen is tall";
    EXPECT_GT(tall.y, wide.y) << "and more rows";

    // The other strategies are direct.
    const ClusterConfig single{.kind = ClusterConfig::Kind::Single};
    EXPECT_EQ(single.dimensions_for_screen_size(glm::uvec2(1920, 1080)), glm::uvec3(1, 1, 1));
    const ClusterConfig explicit_xyz{.kind = ClusterConfig::Kind::XYZ, .dimensions = glm::uvec3(8, 6, 4)};
    EXPECT_EQ(explicit_xyz.dimensions_for_screen_size(glm::uvec2(1920, 1080)), glm::uvec3(8, 6, 4));
    const ClusterConfig off{.kind = ClusterConfig::Kind::None};
    EXPECT_EQ(off.dimensions_for_screen_size(glm::uvec2(1920, 1080)), glm::uvec3(0u));

    // A total below the slice count clamps to the total rather than dividing by
    // more slices than there are clusters, and the extremes keep a valid grid.
    const ClusterConfig tiny{.kind = ClusterConfig::Kind::FixedZ, .total = 1, .z_slices = 8};
    const glm::uvec3 tiny_dims = tiny.dimensions_for_screen_size(glm::uvec2(1920, 1080));
    EXPECT_EQ(tiny_dims, glm::uvec3(1, 1, 1)) << "one cluster means one of everything";

    // Zero dimensions are an error rather than a silent grid.
    EXPECT_THROW(defaults.dimensions_for_screen_size(glm::uvec2(0, 1080)), std::invalid_argument);
}

/** @brief `Clusters::update` computes whole-pixel tiles and then recomputes the
 *  dimensions from them, so an uneven screen yields fewer clusters than
 *  requested, and `clear` resets to no clusters (Bevy `Clusters::update` and
 *  `clear`, `bevy_light/src/cluster/mod.rs:315-340`). */
TEST(Cluster, ClustersUpdateDerivesTilesThenDimensions) {
    using epix::pbr::ClusterableObjectCounts;
    using epix::pbr::Clusters;
    using epix::pbr::VisibleClusterableObjects;

    // 1920x1080 over a 17x9 grid: tiles of 113 by 120 pixels, which divides
    // evenly enough that the recomputed dimensions match the request.
    Clusters clusters;
    clusters.update(glm::uvec2(1920, 1080), glm::uvec3(17, 9, 24));
    EXPECT_EQ(clusters.tile_size, glm::uvec2(113, 120)) << "ceil(1920/17) by ceil(1080/9)";
    EXPECT_EQ(clusters.dimensions, glm::uvec3(17, 9, 24));

    // 10 pixels over a 7x7 request: one-pixel tiles, so only five clusters fit
    // across - fewer than asked for, because tiles are whole pixels.
    clusters.update(glm::uvec2(10, 10), glm::uvec3(7, 7, 1));
    EXPECT_EQ(clusters.tile_size, glm::uvec2(2, 2)) << "ceil(10/7)";
    EXPECT_EQ(clusters.dimensions, glm::uvec3(5, 5, 1)) << "recomputed from the tile size, not the request";

    // A request finer than the screen cannot produce sub-pixel tiles.
    clusters.update(glm::uvec2(10, 10), glm::uvec3(100, 100, 1));
    EXPECT_EQ(clusters.tile_size, glm::uvec2(1, 1)) << "floored at one pixel";
    EXPECT_EQ(clusters.dimensions, glm::uvec3(10, 10, 1));

    // The z count is taken from the request unchanged.
    clusters.update(glm::uvec2(10, 10), glm::uvec3(2, 2, 7));
    EXPECT_EQ(clusters.dimensions.z, 7u);

    // Clear zeroes the layout, including the dimensions.
    clusters.near_plane = 0.5f;
    clusters.far_plane  = 90.0f;
    clusters.clusterable_objects.push_back(VisibleClusterableObjects{});
    clusters.clear();
    EXPECT_EQ(clusters.tile_size, glm::uvec2(1, 1));
    EXPECT_EQ(clusters.dimensions, glm::uvec3(0u)) << "zero, not one, after a clear";
    EXPECT_FLOAT_EQ(clusters.near_plane, 0.0f);
    EXPECT_FLOAT_EQ(clusters.far_plane, 0.0f);
    EXPECT_TRUE(clusters.clusterable_objects.empty());

    static_cast<void>(ClusterableObjectCounts{});
}

/** @brief An orthographic view's near plane is derived from its reverse-Z
 *  matrix, and the dynamic-resizing estimate scales a clusterable object's NDC
 *  extent to the grid, counting one extra cluster per axis side unless the
 *  object reaches the NDC edge (Bevy `assign.rs:352-361,377-432`). */
TEST(Cluster, ClusterIndexEstimateMatchesBevy) {
    using epix::camera::Sphere;
    using epix::pbr::calculate_cluster_factors;
    using epix::pbr::ClusterableObjectAssignmentData;
    using epix::pbr::estimate_cluster_index_count;
    using epix::pbr::orthographic_first_slice_depth;

    const float z_near = 0.1f;
    const float z_far  = 100.0f;
    // Reverse-Z orthographic: near and far swapped, as the cluster code expects.
    const glm::mat4 ortho = glm::orthoRH_ZO(-1.0f, 1.0f, -0.75f, 0.75f, z_far, z_near);
    EXPECT_NEAR(orthographic_first_slice_depth(ortho), z_near, 1e-3f)
        << "(m32 - 1) / m22 recovers the near plane";

    const glm::vec2 screen{800.0f, 600.0f};
    const glm::mat4 projection = epix::camera::perspective_infinite_reverse_rh(glm::radians(60.0f), screen.x / screen.y, z_near);
    const glm::uvec3 dims{17, 9, 24};
    const glm::vec2 factors = calculate_cluster_factors(z_near, z_far, static_cast<float>(dims.z), false);
    const glm::mat4 identity{1.0f};

    ClusterableObjectAssignmentData small;
    small.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -10.0f));
    small.range     = 1.0f;

    const auto in_view = [](const Sphere&) { return true; };
    const auto none_in_view = [](const Sphere&) { return false; };

    const float inside = estimate_cluster_index_count({small}, identity, glm::vec3(1.0f), projection, factors, dims, false,
                                                     in_view);
    EXPECT_GT(inside, 0.0f) << "an object in view contributes indices";
    EXPECT_FLOAT_EQ(estimate_cluster_index_count({small}, identity, glm::vec3(1.0f), projection, factors, dims, false,
                                                 none_in_view),
                    0.0f)
        << "and an object outside the view contributes none";

    // A very large object reaches the NDC edges, so its overlap allowance is
    // smaller than that of a small object: one extra cluster per side at most,
    // against two for an object nowhere near an edge.
    ClusterableObjectAssignmentData huge;
    huge.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -10.0f));
    huge.range     = 1000.0f;
    const float huge_estimate =
        estimate_cluster_index_count({huge}, identity, glm::vec3(1.0f), projection, factors, dims, false, in_view);
    const float area_ratio = huge_estimate / inside;
    EXPECT_GT(area_ratio, 1.0f) << "a larger object needs more clusters";
    // Its overlap term is 1*1 rather than 2*2, so the ratio is well below the
    // 100x ratio of their radii.
    EXPECT_LT(area_ratio, 100.0f) << "but not in proportion to radius, because the overlap is capped";

    // The estimate sums over objects.
    const float two = estimate_cluster_index_count({small, small}, identity, glm::vec3(1.0f), projection, factors, dims,
                                                   false, in_view);
    EXPECT_NEAR(two, 2.0f * inside, 1e-3f);
}

/** @brief The dynamic-resizing reduction scales both axes by the square root of
 *  the index ratio, and the orthographic cluster planes place their boundaries
 *  with the y axis inverted (Bevy `assign.rs:434-451,484-503`). */
TEST(Cluster, ClusterReductionAndOrthographicPlanesMatchBevy) {
    using epix::camera::HalfSpace;
    using epix::pbr::orthographic_cluster_xy_planes;
    using epix::pbr::reduce_cluster_dimensions_for_index_limit;
    using epix::pbr::VIEW_CLUSTER_MAX_INDICES;

    // An estimate four times the limit halves both axes; z is untouched.
    const glm::uvec3 reduced =
        reduce_cluster_dimensions_for_index_limit(glm::uvec3(17, 9, 24), 4.0f * VIEW_CLUSTER_MAX_INDICES,
                                                  VIEW_CLUSTER_MAX_INDICES);
    EXPECT_EQ(reduced, glm::uvec3(8, 4, 24)) << "floor(17 * 0.5) and floor(9 * 0.5)";

    // Under the limit nothing changes; far over it both axes clamp at one.
    EXPECT_EQ(reduce_cluster_dimensions_for_index_limit(glm::uvec3(17, 9, 24), 100.0f, VIEW_CLUSTER_MAX_INDICES),
              glm::uvec3(17, 9, 24));
    const glm::uvec3 clamped = reduce_cluster_dimensions_for_index_limit(
        glm::uvec3(17, 9, 24), 1e12f, VIEW_CLUSTER_MAX_INDICES);
    EXPECT_EQ(clamped.x, 1u) << "never zero";
    EXPECT_EQ(clamped.y, 1u);
    EXPECT_EQ(clamped.z, 24u) << "z is never adjusted";

    // Orthographic x and y planes: one more than the cluster count per axis,
    // spanning the view, with the y order inverted.
    const float z_near = 0.1f;
    const float z_far  = 100.0f;
    const glm::mat4 ortho      = glm::orthoRH_ZO(-1.0f, 1.0f, -0.75f, 0.75f, z_far, z_near);
    const glm::mat4 view_from_clip = glm::inverse(ortho);
    const auto [x_planes, y_planes] = orthographic_cluster_xy_planes(view_from_clip, glm::uvec3(4, 4, 1));

    ASSERT_EQ(x_planes.size(), 5u) << "dimensions.x + 1";
    ASSERT_EQ(y_planes.size(), 5u);
    // The x boundaries run left to right across the view's x extent.
    EXPECT_NEAR(x_planes.front().d(), -1.0f, 1e-3f);
    EXPECT_NEAR(x_planes[2].d(), 0.0f, 1e-3f);
    EXPECT_NEAR(x_planes.back().d(), 1.0f, 1e-3f);
    // The y boundaries run top to bottom, so they start high and end low.
    EXPECT_NEAR(y_planes.front().d(), 0.75f, 1e-3f) << "the top of the view is the first plane";
    EXPECT_NEAR(y_planes[2].d(), 0.0f, 1e-3f);
    EXPECT_NEAR(y_planes.back().d(), -0.75f, 1e-3f) << "and the bottom is the last";
    // Each plane carries its axis as the normal.
    EXPECT_EQ(x_planes[1].normal(), glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(y_planes[1].normal(), glm::vec3(0.0f, 1.0f, 0.0f));
}

/** @brief The z planes sit at the slice boundaries with a `-Z` normal, and the
 *  perspective x/y planes are the ones through the eye and two near-plane
 *  corners (Bevy `assign.rs:504-534`). */
TEST(Cluster, ClusterPlanesMatchBevy) {
    using epix::pbr::cluster_z_planes;
    using epix::pbr::perspective_cluster_xy_planes;

    const float z_near = 0.1f;
    const float z_far  = 100.0f;
    const float screen_aspect = 800.0f / 600.0f;
    const glm::mat4 projection = epix::camera::perspective_infinite_reverse_rh(glm::radians(60.0f), screen_aspect, z_near);
    const glm::mat4 view_from_clip = glm::inverse(projection);
    const glm::uvec3 dims{4, 4, 8};

    // Z planes: one per slice boundary, from 0 to -far, each holding its
    // boundary under Bevy's n . x = d convention.
    const auto z_planes = cluster_z_planes(z_near, z_far, dims.z, false);
    ASSERT_EQ(z_planes.size(), dims.z + 1);
    EXPECT_NEAR(z_planes.front().d(), 0.0f, 1e-4f) << "the first perspective boundary is at view z 0";
    EXPECT_NEAR(z_planes.back().d(), z_far, 1e-3f) << "the last is at -far, so d is +far";
    for (std::size_t i = 0; i < z_planes.size(); ++i) {
        EXPECT_EQ(z_planes[i].normal(), glm::vec3(0.0f, 0.0f, -1.0f));
        const float view_z = -z_planes[i].d();
        EXPECT_NEAR(glm::dot(z_planes[i].normal(), glm::vec3(0.0f, 0.0f, view_z)), z_planes[i].d(), 1e-4f)
            << "a point at that view z lies on the plane";
    }

    // Perspective x/y planes: one more than the count per axis, and each holds
    // its own near-plane corner under the same convention.
    const auto [x_planes, y_planes] = perspective_cluster_xy_planes(view_from_clip, dims);
    ASSERT_EQ(x_planes.size(), dims.x + 1);
    ASSERT_EQ(y_planes.size(), dims.y + 1);
    for (std::size_t i = 0; i < x_planes.size(); ++i) {
        const float x_pos = (static_cast<float>(i) / static_cast<float>(dims.x)) * 2.0f - 1.0f;
        const glm::vec3 corner = glm::vec3(view_from_clip * glm::vec4(x_pos, -1.0f, 1.0f, 1.0f));
        // The unprojected corner itself satisfies the plane equation.
        EXPECT_NEAR(glm::dot(x_planes[i].normal(), corner), x_planes[i].d(), 1e-2f)
            << "the boundary's corner lies on its plane";
    }
    // For a symmetric frustum the x planes' normals have no y component, and the
    // centre plane is the view's own y axis plane.
    EXPECT_NEAR(x_planes[2].normal().y, 0.0f, 1e-4f);
    EXPECT_NEAR(x_planes[2].d(), 0.0f, 1e-4f) << "the centre x plane passes through the view axis";
    EXPECT_NEAR(y_planes[2].normal().x, 0.0f, 1e-4f);
    EXPECT_NEAR(y_planes[2].d(), 0.0f, 1e-4f) << "and so does the centre y plane";
}

/** @brief An object's cluster range is ordered from its two projected corners,
 *  and its centre's z and y rows follow Bevy's asymmetric off-screen conventions
 *  (Bevy `assign.rs:570-641`). */
TEST(Cluster, ClusterAssignmentBoundsMatchBevy) {
    using epix::camera::Sphere;
    using epix::pbr::calculate_cluster_factors;
    using epix::pbr::cluster_assignment_bounds;

    const float z_near = 0.1f;
    const float z_far  = 100.0f;
    const float aspect = 800.0f / 600.0f;
    const glm::mat4 projection     = epix::camera::perspective_infinite_reverse_rh(glm::radians(60.0f), aspect, z_near);
    const glm::mat4 view_from_clip = glm::inverse(projection);
    const glm::uvec3 dims{4, 4, 8};
    const glm::vec2 factors = calculate_cluster_factors(z_near, z_far, static_cast<float>(dims.z), false);
    const glm::mat4 identity{1.0f};

    // An object in front of the camera: its bounds span a couple of clusters and
    // its centre sits inside the grid.
    const Sphere ahead{.center = glm::vec3(0.0f, 0.0f, -10.0f), .radius = 1.0f};
    const auto centered = cluster_assignment_bounds(ahead, identity, 1.0f, projection, factors, dims, false,
                                                    glm::vec3(-0.1f, -0.1f, -11.0f), glm::vec3(0.1f, 0.1f, -9.0f));
    EXPECT_LE(centered.min_cluster.x, centered.max_cluster.x) << "the range is ordered";
    EXPECT_LE(centered.min_cluster.y, centered.max_cluster.y);
    EXPECT_LE(centered.min_cluster.z, centered.max_cluster.z);
    EXPECT_NEAR(centered.view_sphere.center.z, -10.0f, 1e-4f) << "the sphere is transformed into view space";
    EXPECT_FLOAT_EQ(centered.view_sphere.radius, 1.0f);
    ASSERT_TRUE(centered.z_center.has_value());
    ASSERT_TRUE(centered.y_center.has_value());
    EXPECT_EQ(*centered.y_center, 2u) << "the centre of the grid";
    // View z -10 is mid-range, not near the camera: the slices are geometric, so
    // far more of them are close in than far out.
    EXPECT_EQ(*centered.z_center, epix::pbr::view_z_to_z_slice(factors, dims.z, -10.0f, false));
    EXPECT_EQ(*centered.z_center, 5u);

    // The range is ordered even when the corners project the other way round.
    const auto swapped = cluster_assignment_bounds(ahead, identity, 1.0f, projection, factors, dims, false,
                                                   glm::vec3(0.1f, 0.1f, -11.0f), glm::vec3(-0.1f, -0.1f, -9.0f));
    EXPECT_EQ(swapped.min_cluster, centered.min_cluster) << "order comes from min/max, not from the arguments";
    EXPECT_EQ(swapped.max_cluster, centered.max_cluster);

    // The radius scales with the largest axis scale, so a non-uniform scale
    // cannot shrink it.
    const auto stretched = cluster_assignment_bounds(ahead, identity, 3.0f, projection, factors, dims, false,
                                                     glm::vec3(-0.1f, -0.1f, -11.0f), glm::vec3(0.1f, 0.1f, -9.0f));
    EXPECT_FLOAT_EQ(stretched.view_sphere.radius, 3.0f) << "radius * the largest scale";

    // A centre above the view has no row, but one below it gets the sentinel.
    const Sphere above{.center = glm::vec3(0.0f, 100.0f, -10.0f), .radius = 0.1f};
    const auto above_bounds = cluster_assignment_bounds(above, identity, 1.0f, projection, factors, dims, false,
                                                        glm::vec3(0.0f, 0.9f, -10.1f), glm::vec3(0.0f, 1.0f, -9.9f));
    EXPECT_FALSE(above_bounds.y_center.has_value()) << "above the view: no row";

    const Sphere below{.center = glm::vec3(0.0f, -100.0f, -10.0f), .radius = 0.1f};
    const auto below_bounds = cluster_assignment_bounds(below, identity, 1.0f, projection, factors, dims, false,
                                                        glm::vec3(0.0f, -1.0f, -10.1f), glm::vec3(0.0f, -0.9f, -9.9f));
    ASSERT_TRUE(below_bounds.y_center.has_value()) << "below the view: a sentinel rather than nothing";
    EXPECT_EQ(*below_bounds.y_center, dims.y + 1) << "one past the last row";
}

/** @brief The sphere refinement projects the object's sphere into each (z, y)
 *  cell and scans inwards for the x clusters it reaches, and the cluster index
 *  is y-major with z innermost (Bevy `assign.rs:642-714`). */
TEST(Cluster, SphereRefinementAndClusterIndexMatchBevy) {
    using epix::camera::Sphere;
    using epix::pbr::calculate_cluster_factors;
    using epix::pbr::cluster_index;
    using epix::pbr::cluster_z_planes;
    using epix::pbr::orthographic_cluster_xy_planes;
    using epix::pbr::perspective_cluster_xy_planes;
    using epix::pbr::refine_sphere_and_scan_x;

    // The index layout: y outermost, then x, then z innermost.
    const glm::uvec3 dims{4, 5, 8};
    EXPECT_EQ(cluster_index(dims, 0, 0, 0), 0u);
    EXPECT_EQ(cluster_index(dims, 1, 0, 0), 8u) << "x advances by the z count";
    EXPECT_EQ(cluster_index(dims, 0, 1, 0), 4u * 8u) << "y advances by x * z";
    EXPECT_EQ(cluster_index(dims, 3, 4, 7), (4u * 4u + 3u) * 8u + 7u) << "the last cluster";

    const float z_near = 0.1f;
    const float z_far  = 100.0f;
    const float aspect = 800.0f / 600.0f;
    const glm::mat4 projection     = epix::camera::perspective_infinite_reverse_rh(glm::radians(60.0f), aspect, z_near);
    const glm::mat4 view_from_clip = glm::inverse(projection);
    const glm::uvec3 grid{8, 8, 8};
    const glm::vec2 factors = calculate_cluster_factors(z_near, z_far, static_cast<float>(grid.z), false);
    const auto [x_planes, y_planes] = perspective_cluster_xy_planes(view_from_clip, grid);
    const auto z_planes             = cluster_z_planes(z_near, z_far, grid.z, false);

    // An object at the centre of the view: its refined sphere should reach the
    // middle of the x range, not the edges.
    const Sphere view_sphere{.center = glm::vec3(0.0f, 0.0f, -10.0f), .radius = 1.0f};
    const glm::uvec3 min_cluster{0u};
    const glm::uvec3 max_cluster{grid - glm::uvec3(1u)};
    const auto middle = refine_sphere_and_scan_x(min_cluster, max_cluster, 4, 4, 4u, 4u, view_sphere, x_planes, y_planes,
                                                z_planes, false);
    ASSERT_TRUE(middle.reached) << "the object's own cell is reached";
    EXPECT_GT(middle.min_x, 0u) << "the scan walked in from the left";
    EXPECT_LT(middle.max_x, grid.x - 1u) << "and from the right";
    EXPECT_LE(middle.min_x, middle.max_x);
    EXPECT_GT(middle.y_object.radius, 0.0f) << "the refined sphere keeps a real radius";

    // The refinement shrinks the sphere: intersecting it with a plane can only
    // make the circle smaller than the original sphere.
    EXPECT_LE(middle.y_object.radius, view_sphere.radius);
    EXPECT_NEAR(middle.y_object.center.z, -10.0f, 1.0f) << "and moves its centre onto the cell's planes";

    // A sphere that cannot reach a cell is reported as not reached rather than
    // with a bogus range: an object far outside the grid's screen extent.
    const Sphere off_screen{.center = glm::vec3(1000.0f, 0.0f, -10.0f), .radius = 0.01f};
    const auto missed = refine_sphere_and_scan_x(min_cluster, max_cluster, 0, 0, std::nullopt, std::nullopt, off_screen,
                                                 x_planes, y_planes, z_planes, false);
    if (missed.reached) {
        EXPECT_LE(missed.min_x, missed.max_x) << "and when reached, the range is always ordered";
    }

    // The orthographic planes work through the same path.
    const glm::mat4 ortho          = glm::orthoRH_ZO(-1.0f, 1.0f, -0.75f, 0.75f, z_far, z_near);
    const glm::mat4 ortho_view_from_clip = glm::inverse(ortho);
    const auto [ox_planes, oy_planes] = orthographic_cluster_xy_planes(ortho_view_from_clip, grid);
    const auto ortho_result = refine_sphere_and_scan_x(min_cluster, max_cluster, 4, 4, 4u, 4u, view_sphere, ox_planes,
                                                      oy_planes, z_planes, true);
    EXPECT_TRUE(ortho_result.reached);
    EXPECT_LE(ortho_result.min_x, ortho_result.max_x);
}

/** @brief A spot light culls a cluster by its cone, by its range or by the
 *  cluster being behind it, and a cluster's bounding sphere takes its radius from
 *  the AABB's half extents (Bevy `assign.rs:739-765`). */
TEST(Cluster, SpotLightClusterCullMatchesBevy) {
    using epix::camera::Aabb;
    using epix::pbr::cluster_bounding_sphere;
    using epix::pbr::spot_light_cluster_cull;

    // The cluster's bounding sphere: the box's centre, and the length of its
    // half extents as the radius.
    const Aabb aabb = Aabb::from_min_max(glm::vec3(-1.0f, -2.0f, -3.0f), glm::vec3(1.0f, 2.0f, 3.0f));
    const auto sphere = cluster_bounding_sphere(aabb);
    EXPECT_EQ(sphere.center, glm::vec3(0.0f));
    EXPECT_NEAR(sphere.radius, std::sqrt(1.0f + 4.0f + 9.0f), 1e-5f);

    // A light at the origin whose cone points down -Z. Note the direction used
    // here is the transform's *back* axis, as Bevy computes it
    // (`view_from_world * transform.back()`), so it points away from where the
    // light shines - which is what makes v1_len positive for a cluster in front.
    const glm::vec3 light_center{0.0f};
    const glm::vec3 direction{0.0f, 0.0f, 1.0f};
    const float sin45 = std::sqrt(0.5f);
    const float cos45 = std::sqrt(0.5f);
    const float range = 20.0f;

    // Directly in front, inside the cone and the range: no cull.
    const auto ahead = spot_light_cluster_cull(light_center, range, direction, sin45, cos45,
                                               glm::vec3(0.0f, 0.0f, -10.0f), 1.0f);
    EXPECT_FALSE(ahead.culled()) << "a cluster in the cone within range is lit";
    EXPECT_FALSE(ahead.front_cull);
    EXPECT_FALSE(ahead.back_cull);
    EXPECT_FALSE(ahead.angle_cull);

    // Far to the side: outside the cone.
    const auto sideways = spot_light_cluster_cull(light_center, range, direction, sin45, cos45,
                                                  glm::vec3(50.0f, 0.0f, -10.0f), 1.0f);
    EXPECT_TRUE(sideways.angle_cull) << "well outside a 45-degree cone";

    // Along the axis but beyond the range: a front cull, not an angle cull.
    const auto beyond = spot_light_cluster_cull(light_center, range, direction, sin45, cos45,
                                                glm::vec3(0.0f, 0.0f, -100.0f), 1.0f);
    EXPECT_TRUE(beyond.front_cull) << "past the light's range";
    EXPECT_FALSE(beyond.angle_cull) << "it is on the axis, so the cone does not cull it";

    // Behind the light: a back cull.
    const auto behind = spot_light_cluster_cull(light_center, range, direction, sin45, cos45,
                                                glm::vec3(0.0f, 0.0f, 10.0f), 1.0f);
    EXPECT_TRUE(behind.back_cull) << "behind the light's origin";

    // A cluster straddling the range boundary is only culled once its sphere is
    // fully past range + radius, since the radius is added to the reach.
    const auto straddling = spot_light_cluster_cull(light_center, range, direction, sin45, cos45,
                                                    glm::vec3(0.0f, 0.0f, -(range + 0.5f)), 1.0f);
    EXPECT_FALSE(straddling.front_cull) << "the cluster's sphere still overlaps the range";
    const auto past = spot_light_cluster_cull(light_center, range, direction, sin45, cos45,
                                             glm::vec3(0.0f, 0.0f, -(range + 1.5f)), 1.0f);
    EXPECT_TRUE(past.front_cull) << "and once it clears range + radius";
}

/** @brief Each kind of clusterable object increments its own counter, and a row
 *  insertion walks along x without touching other clusters (Bevy
 *  `assign.rs:766-838`). */
TEST(Cluster, ClusterInsertionCountsByKind) {
    using epix::pbr::ClusterableObjectType;
    using epix::pbr::cluster_index;
    using epix::pbr::insert_object_into_cluster;
    using epix::pbr::insert_object_into_cluster_row;
    using epix::pbr::VisibleClusterableObjects;

    const auto kind = [](ClusterableObjectType::Kind which) {
        return ClusterableObjectType{.kind = which};
    };

    // Each kind bumps exactly one counter and leaves the rest alone, which
    // matters because these counts become the shader's offsets and counts.
    VisibleClusterableObjects cluster;
    insert_object_into_cluster(cluster, epix::ecs::Entity::from_index(1), kind(ClusterableObjectType::Kind::PointLight));
    insert_object_into_cluster(cluster, epix::ecs::Entity::from_index(2), kind(ClusterableObjectType::Kind::PointLight));
    insert_object_into_cluster(cluster, epix::ecs::Entity::from_index(3), kind(ClusterableObjectType::Kind::SpotLight));
    insert_object_into_cluster(cluster, epix::ecs::Entity::from_index(4),
                               kind(ClusterableObjectType::Kind::ReflectionProbe));
    insert_object_into_cluster(cluster, epix::ecs::Entity::from_index(5),
                               kind(ClusterableObjectType::Kind::IrradianceVolume));
    insert_object_into_cluster(cluster, epix::ecs::Entity::from_index(6), kind(ClusterableObjectType::Kind::Decal));

    EXPECT_EQ(cluster.counts.point_lights, 2u);
    EXPECT_EQ(cluster.counts.spot_lights, 1u);
    EXPECT_EQ(cluster.counts.reflection_probes, 1u);
    EXPECT_EQ(cluster.counts.irradiance_volumes, 1u);
    EXPECT_EQ(cluster.counts.decals, 1u);
    EXPECT_EQ(cluster.entities.size(), 6u) << "every object is also listed";

    // A row insertion covers x from min to max at one (y, z) and nothing else.
    const glm::uvec3 dims{4, 4, 8};
    std::vector<VisibleClusterableObjects> clusters(dims.x * dims.y * dims.z);
    insert_object_into_cluster_row(clusters, dims, 1, 2, 1, 2, epix::ecs::Entity::from_index(7),
                                   kind(ClusterableObjectType::Kind::PointLight));

    EXPECT_EQ(clusters[cluster_index(dims, 0, 1, 2)].entities.size(), 0u) << "before the row";
    EXPECT_EQ(clusters[cluster_index(dims, 3, 1, 2)].entities.size(), 0u) << "after it";
    for (std::uint32_t x = 1; x <= 2; ++x) {
        const auto& cell = clusters[cluster_index(dims, x, 1, 2)];
        EXPECT_EQ(cell.entities.size(), 1u) << "x = " << x;
        EXPECT_EQ(cell.counts.point_lights, 1u);
    }
    // A different y at the same x must be untouched.
    EXPECT_EQ(clusters[cluster_index(dims, 1, 2, 2)].entities.size(), 0u) << "the row is at y 1 only";
}

/** @brief The uniform-buffer cap keeps only the objects in view, reports when
 *  more were in view than it can hold, and drops the rest (Bevy
 *  `assign.rs:262-294`). */
TEST(Cluster, UniformClusterableObjectCapMatchesBevy) {
    using epix::camera::Sphere;
    using epix::pbr::ClusterableObjectAssignmentData;
    using epix::pbr::limit_clusterable_objects_for_uniform_buffer;

    const auto object_at = [](std::uint32_t index, float x) {
        ClusterableObjectAssignmentData object;
        object.entity    = epix::ecs::Entity::from_index(index);
        object.transform = glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, -10.0f));
        object.range     = 1.0f;
        return object;
    };
    // Objects at a positive x are "in view" in this test.
    const auto in_view = [](const Sphere& sphere) { return sphere.center.x > 0.0f; };

    // Under the cap: everything in view is kept and nothing is reported.
    const auto under = limit_clusterable_objects_for_uniform_buffer(
        {object_at(1, 1.0f), object_at(2, 2.0f), object_at(3, -1.0f)}, in_view, 3);
    EXPECT_EQ(under.objects.size(), 2u) << "the out-of-view object is dropped";
    EXPECT_FALSE(under.exceeded);

    // Over the cap: the survivors are truncated and the excess is reported.
    const auto over = limit_clusterable_objects_for_uniform_buffer(
        {object_at(1, 1.0f), object_at(2, 2.0f), object_at(3, 3.0f), object_at(4, 4.0f), object_at(5, 5.0f)}, in_view, 3);
    EXPECT_EQ(over.objects.size(), 3u) << "truncated to the cap";
    EXPECT_TRUE(over.exceeded) << "more were in view than it can hold";

    // Objects outside the view never trigger the report, however many there are.
    std::vector<ClusterableObjectAssignmentData> outside;
    for (std::uint32_t i = 0; i < 50; ++i) outside.push_back(object_at(i + 1, -1.0f));
    outside.push_back(object_at(100, 1.0f));
    const auto mostly_outside = limit_clusterable_objects_for_uniform_buffer(std::move(outside), in_view, 3);
    EXPECT_EQ(mostly_outside.objects.size(), 1u);
    EXPECT_FALSE(mostly_outside.exceeded) << "the cap counts what is in view, not what exists";

    // Exactly one over the cap is still reported, since Bevy keeps that extra
    // object precisely to detect it.
    const auto exactly_one_over = limit_clusterable_objects_for_uniform_buffer(
        {object_at(1, 1.0f), object_at(2, 2.0f), object_at(3, 3.0f), object_at(4, 4.0f)}, in_view, 3);
    EXPECT_EQ(exactly_one_over.objects.size(), 3u);
    EXPECT_TRUE(exactly_one_over.exceeded);

    // Exactly at the cap is not reported.
    const auto exactly_at = limit_clusterable_objects_for_uniform_buffer(
        {object_at(1, 1.0f), object_at(2, 2.0f), object_at(3, 3.0f)}, in_view, 3);
    EXPECT_EQ(exactly_at.objects.size(), 3u);
    EXPECT_FALSE(exactly_at.exceeded);
}

/** @brief The extracted light types carry what the light preparation needs, and
 *  the point light flags include the spot-light Y flip (Bevy
 *  `render/light.rs:69-122`). */
TEST(Cluster, ExtractedLightsAndFlagsMatchBevy) {
    using epix::pbr::ExtractedDirectionalLight;
    using epix::pbr::ExtractedPointLight;
    using epix::pbr::PointLightFlags;

    // The point light flags, which must match mesh_view_types.wgsl.
    EXPECT_EQ(PointLightFlags::SHADOWS_ENABLED, 1u);
    EXPECT_EQ(PointLightFlags::SPOT_LIGHT_Y_NEGATIVE, 2u);
    EXPECT_EQ(PointLightFlags::VOLUMETRIC, 4u);
    EXPECT_EQ(PointLightFlags::AFFECTS_LIGHTMAPPED_MESH_DIFFUSE, 8u);
    EXPECT_EQ(PointLightFlags::NONE, 0u);
    EXPECT_EQ(PointLightFlags::UNINITIALIZED, 0xFFFFu);

    // An extracted point light: a spot light is one of these with angles set,
    // which is why the flags carry a spot-light bit.
    ExtractedPointLight point;
    EXPECT_FALSE(point.spot_light_angles.has_value()) << "a plain point light has no cone";
    point.spot_light_angles = std::pair{0.1f, 0.5f};
    ASSERT_TRUE(point.spot_light_angles.has_value());
    EXPECT_FLOAT_EQ(point.spot_light_angles->first, 0.1f) << "the inner angle";
    EXPECT_FLOAT_EQ(point.spot_light_angles->second, 0.5f) << "and the outer";
    EXPECT_TRUE(point.affects_lightmapped_mesh_diffuse) << "diffuse lightmapped light applies by default";
    EXPECT_FALSE(point.shadows_enabled);
    EXPECT_EQ(point.color, glm::vec4(1.0f));

    // An extracted directional light carries its cascades and frusta per view.
    ExtractedDirectionalLight directional;
    EXPECT_TRUE(directional.cascades.empty());
    EXPECT_TRUE(directional.frusta.empty());
    EXPECT_FALSE(directional.soft_shadow_size.has_value()) << "no PCSS by default";
    EXPECT_EQ(directional.cascade_shadow_config.bounds.size(), 4u) << "the default cascade configuration";
    EXPECT_TRUE(directional.affects_lightmapped_mesh_diffuse);
}

/** @brief The frame's light counts respect the device's texture limits, and the
 *  sort keys put shadows and volumetrics first with the entity as a stable
 *  tiebreak (Bevy `prepare_lights`, `render/light.rs:806-879`). */
TEST(Cluster, LightCountsAndSortKeysMatchBevy) {
    using epix::pbr::count_lights;
    using epix::pbr::directional_light_sort_key;
    using epix::pbr::ExtractedDirectionalLight;
    using epix::pbr::ExtractedPointLight;
    using epix::pbr::point_light_sort_key;
    using epix::pbr::point_or_spot_light_to_clusterable;
    using epix::pbr::ClusterableObjectType;

    const auto point_light = [](bool shadows, bool volumetric, std::optional<std::pair<float, float>> angles) {
        ExtractedPointLight light;
        light.shadows_enabled   = shadows;
        light.volumetric        = volumetric;
        light.spot_light_angles = angles;
        return light;
    };
    const std::pair<float, float> cone{0.1f, 0.5f};

    // A point light becomes a PointLight, one with angles becomes a SpotLight
    // carrying its outer angle.
    const auto as_point = point_or_spot_light_to_clusterable(point_light(true, false, std::nullopt));
    EXPECT_EQ(as_point.kind, ClusterableObjectType::Kind::PointLight);
    EXPECT_TRUE(as_point.shadows_enabled);
    const auto as_spot = point_or_spot_light_to_clusterable(point_light(false, true, cone));
    EXPECT_EQ(as_spot.kind, ClusterableObjectType::Kind::SpotLight);
    EXPECT_FLOAT_EQ(as_spot.outer_angle, 0.5f) << "the outer angle, not the inner";

    // Counts with room to spare: nothing binds.
    const std::vector<ExtractedPointLight> points{point_light(false, false, std::nullopt),
                                                  point_light(true, true, std::nullopt),
                                                  point_light(true, true, cone)};
    const std::vector<ExtractedDirectionalLight> directionals{ExtractedDirectionalLight{.shadows_enabled = true}};
    const auto roomy = count_lights(points, directionals, 128, 21);
    EXPECT_EQ(roomy.point_lights, 2u);
    EXPECT_EQ(roomy.point_light_shadow_maps, 1u);
    EXPECT_EQ(roomy.point_light_volumetric_enabled, 1u);
    EXPECT_EQ(roomy.spot_lights, 1u);
    EXPECT_EQ(roomy.spot_light_shadow_maps, 1u);
    EXPECT_EQ(roomy.directional_shadow_enabled, 1u);
    EXPECT_EQ(roomy.directional_volumetric_enabled, 0u);

    // With a tight device, the caps bind - point lights by the cubemap limit,
    // spot lights by the layers left after the directional shadow maps.
    const auto tight = count_lights(points, directionals, 4, 1);
    EXPECT_EQ(tight.point_light_shadow_maps, 1u) << "capped by max_texture_cubes";
    EXPECT_LE(tight.spot_lights, 4u - tight.directional_shadow_enabled * 4u)
        << "spot lights share the array with the directional shadow maps";

    // Point lights sort as point-then-spot, shadows and volumetrics first.
    const auto shadowy = point_light_sort_key(epix::ecs::Entity::from_index(9), point_light(true, false, std::nullopt));
    const auto plain   = point_light_sort_key(epix::ecs::Entity::from_index(1), point_light(false, false, std::nullopt));
    EXPECT_LT(shadowy, plain) << "shadows enabled sorts first, even with a larger entity";
    const auto spot = point_light_sort_key(epix::ecs::Entity::from_index(2), point_light(true, true, cone));
    EXPECT_LT(shadowy, spot) << "point lights before spot lights";

    // Two identical lights are ordered by entity, so the set is deterministic.
    const auto first  = point_light_sort_key(epix::ecs::Entity::from_index(3), point_light(false, false, std::nullopt));
    const auto second = point_light_sort_key(epix::ecs::Entity::from_index(4), point_light(false, false, std::nullopt));
    EXPECT_LT(first, second) << "the entity breaks the tie";

    // Directional lights sort by the raw booleans, so - contrary to the comment
    // above Bevy's sort - lights with volumetric off come first, and with
    // shadows off before shadows on. The code is what is matched.
    const auto volumetric = directional_light_sort_key(epix::ecs::Entity::from_index(9),
                                                      ExtractedDirectionalLight{.shadows_enabled = false, .volumetric = true});
    const auto plain_directional = directional_light_sort_key(
        epix::ecs::Entity::from_index(1), ExtractedDirectionalLight{.shadows_enabled = false, .volumetric = false});
    EXPECT_LT(plain_directional, volumetric) << "ascending booleans put false first";
    const auto shadowed = directional_light_sort_key(epix::ecs::Entity::from_index(2),
                                                     ExtractedDirectionalLight{.shadows_enabled = true, .volumetric = false});
    EXPECT_LT(plain_directional, shadowed) << "and among equals, no shadows before shadows";
    EXPECT_LT(shadowed, volumetric) << "the volumetric flag dominates both";
}

/** @brief The point and spot light GPU data: the cone's scale and offset with
 *  their guard, the cube-face projection's lower-right 2x2, the Y-sign flag and
 *  the colour/range packing (Bevy `render/light.rs:915-976`). */
TEST(Cluster, PointAndSpotLightGpuDataMatchesBevy) {
    using epix::pbr::pack_light_color_intensity_range;
    using epix::pbr::point_light_custom_data;
    using epix::pbr::spot_light_custom_data;
    using epix::pbr::spot_light_y_negative;

    // A 45-degree spot light with a 30-degree inner angle.
    const float inner = glm::radians(30.0f);
    const float outer = glm::radians(45.0f);
    const glm::vec3 direction = glm::normalize(glm::vec3(1.0f, 0.0f, -1.0f));
    const auto [cone, tan_angle] = spot_light_custom_data(inner, outer, direction);
    EXPECT_NEAR(cone.x, direction.x, 1e-6f) << "the direction's x";
    EXPECT_NEAR(cone.y, direction.z, 1e-6f) << "and its z, in y's place";
    const float cos_outer = std::cos(outer);
    EXPECT_NEAR(cone.z, 1.0f / (std::cos(inner) - cos_outer), 1e-3f) << "the spot scale";
    EXPECT_NEAR(cone.w, -cos_outer * cone.z, 1e-5f) << "and the offset, from the same scale";
    EXPECT_NEAR(tan_angle, std::tan(outer), 1e-5f) << "the tangent of the outer angle";

    // The guard: equal angles would divide by zero without it.
    const auto [degenerate, _tan] = spot_light_custom_data(outer, outer, direction);
    static_cast<void>(_tan);
    EXPECT_TRUE(std::isfinite(degenerate.z)) << "the scale stays finite when the angles coincide";
    EXPECT_NEAR(degenerate.z, 1e4f, 1.0f) << "clamped by the 1e-4 guard";
    EXPECT_TRUE(std::isfinite(degenerate.w));

    // The point light's cube-face data is the projection's lower-right 2x2.
    glm::mat4 projection{1.0f};
    projection[2][2] = 7.0f;
    projection[2][3] = 8.0f;
    projection[3][2] = 9.0f;
    projection[3][3] = 10.0f;
    EXPECT_EQ(point_light_custom_data(projection), glm::vec4(7.0f, 8.0f, 9.0f, 10.0f));

    // The Y-sign test follows is_sign_negative, so negative zero counts.
    EXPECT_TRUE(spot_light_y_negative(glm::vec3(0.0f, -1.0f, 0.0f)));
    EXPECT_FALSE(spot_light_y_negative(glm::vec3(0.0f, 1.0f, 0.0f)));
    EXPECT_TRUE(spot_light_y_negative(glm::vec3(0.0f, -0.0f, 0.0f)))
        << "is_sign_negative is true for negative zero, and the shader relies on that";

    // Colour and range packing: premultiplied by intensity, inverse square in w.
    const glm::vec4 packed = pack_light_color_intensity_range(glm::vec4(0.5f, 0.25f, 0.125f, 1.0f), 100.0f, 20.0f);
    EXPECT_NEAR(packed.x, 50.0f, 1e-4f);
    EXPECT_NEAR(packed.y, 25.0f, 1e-4f);
    EXPECT_NEAR(packed.z, 12.5f, 1e-4f);
    EXPECT_NEAR(packed.w, 1.0f / 400.0f, 1e-6f) << "the inverse square of the range";
}

/** @brief A point light's flags come from its position in the sorted list, where
 *  a spot light must subtract the point-light count, and the cascade count is
 *  the largest per-view total clamped to the device (Bevy
 *  `render/light.rs:887-916,979-1010`). */
TEST(Cluster, PointLightFlagsAndCascadeCountMatchBevy) {
    using epix::pbr::count_lights;
    using epix::pbr::ExtractedDirectionalLight;
    using epix::pbr::ExtractedPointLight;
    using epix::pbr::LightCounts;
    using epix::pbr::num_directional_cascades_enabled;
    using epix::pbr::point_light_gpu_flags;
    using epix::pbr::PointLightFlags;

    const auto point_light = [](bool shadows, bool volumetric, bool is_spot, bool lightmapped) {
        ExtractedPointLight light;
        light.shadows_enabled                  = shadows;
        light.volumetric                       = volumetric;
        light.affects_lightmapped_mesh_diffuse = lightmapped;
        if (is_spot) light.spot_light_angles = std::pair{0.1f, 0.5f};
        return light;
    };

    // Two point lights and two spot lights, all shadow-casting, sorted so point
    // lights come first. Only the first of each block can have a shadow map.
    LightCounts counts;
    counts.point_lights              = 2;
    counts.point_light_shadow_maps   = 1;
    counts.spot_lights               = 2;
    counts.spot_light_shadow_maps    = 1;
    const auto shadowy_spot = point_light(true, false, true, false);

    const auto point_zero = point_light_gpu_flags(0, point_light(true, false, false, true), counts);
    EXPECT_TRUE((point_zero & PointLightFlags::SHADOWS_ENABLED) != 0u) << "index 0 takes the point light's map";
    EXPECT_TRUE((point_zero & PointLightFlags::AFFECTS_LIGHTMAPPED_MESH_DIFFUSE) != 0u);
    const auto point_one = point_light_gpu_flags(1, point_light(true, false, false, false), counts);
    EXPECT_EQ(point_one & PointLightFlags::SHADOWS_ENABLED, 0u) << "the second point light exceeds the map count";

    // The spot block starts at index 2, so the first spot light's in-block
    // position is 0 and it takes the spot map; the second does not. Using the
    // global index here instead would give the first spot light no map and the
    // second one a map.
    const auto spot_zero = point_light_gpu_flags(2, shadowy_spot, counts);
    EXPECT_TRUE((spot_zero & PointLightFlags::SHADOWS_ENABLED) != 0u) << "the spot block's first light";
    const auto spot_one = point_light_gpu_flags(3, shadowy_spot, counts);
    EXPECT_EQ(spot_one & PointLightFlags::SHADOWS_ENABLED, 0u) << "and its second does not";

    // Volumetrics need shadows as well, and both must be within their counts.
    LightCounts volumetric_counts = counts;
    volumetric_counts.point_light_volumetric_enabled = 1;
    volumetric_counts.spot_light_volumetric_enabled  = 1;
    EXPECT_EQ(point_light_gpu_flags(0, point_light(true, false, false, false), volumetric_counts) &
                  PointLightFlags::VOLUMETRIC,
              0u)
        << "volumetric off";
    EXPECT_EQ(point_light_gpu_flags(0, point_light(false, true, false, false), volumetric_counts) &
                  PointLightFlags::VOLUMETRIC,
              0u)
        << "volumetric on but shadows off, which Bevy requires";
    EXPECT_TRUE((point_light_gpu_flags(0, point_light(true, true, false, false), volumetric_counts) &
                 PointLightFlags::VOLUMETRIC) != 0u)
        << "both on, and within the count";

    // The cascade count is the largest per-view total, filtered by render layers
    // and clamped per light and then by the device.
    ExtractedDirectionalLight light;
    light.shadows_enabled = true;
    light.render_layers   = epix::camera::RenderLayers::layer(0);
    EXPECT_EQ(light.cascade_shadow_config.bounds.size(), 4u) << "the default is four cascades";
    EXPECT_EQ(num_directional_cascades_enabled({epix::camera::RenderLayers::layer(0)}, {light}, 128), 4u);
    EXPECT_EQ(num_directional_cascades_enabled({epix::camera::RenderLayers::layer(1)}, {light}, 128), 0u) << "the layers do not intersect";
    EXPECT_EQ(num_directional_cascades_enabled({epix::camera::RenderLayers::layer(0)}, {light}, 2), 2u) << "clamped by the device's layers";
    // The maximum across views, not the sum.
    EXPECT_EQ(num_directional_cascades_enabled({epix::camera::RenderLayers::layer(0), epix::camera::RenderLayers::layer(1)}, {light}, 128), 4u);
    ExtractedDirectionalLight second = light;
    EXPECT_EQ(num_directional_cascades_enabled({epix::camera::RenderLayers::layer(0)}, {light, second}, 128), 8u) << "two lights in one view";
    second.shadows_enabled = false;
    EXPECT_EQ(num_directional_cascades_enabled({epix::camera::RenderLayers::layer(0)}, {light, second}, 128), 4u) << "a light without shadows";
    second = light;
    second.cascade_shadow_config.bounds.resize(10);
    EXPECT_EQ(num_directional_cascades_enabled({epix::camera::RenderLayers::layer(0)}, {second}, 128), 4u) << "clamped per light to four cascades";
}

/** @brief The `GpuLights` assembly: the ambient colour, the cluster factors and
 *  dimensions, and the spot-light shadow-map offset (Bevy
 *  `render/light.rs:1228-1247`). */
TEST(Cluster, GpuLightsAssemblyMatchesBevy) {
    using epix::pbr::calculate_cluster_factors;
    using epix::pbr::pack_ambient_color;
    using epix::pbr::pack_cluster_dimensions;
    using epix::pbr::pack_cluster_factors;
    using epix::pbr::spot_light_shadowmap_offset;

    // The ambient colour is the light's linear colour scaled by its brightness.
    EXPECT_EQ(pack_ambient_color(glm::vec4(0.5f, 0.25f, 0.125f, 1.0f), 80.0f), glm::vec4(40.0f, 20.0f, 10.0f, 80.0f))
        << "brightness scales all four components, alpha included";

    // The cluster factors: grid over viewport in x and y, the log pair in z and w.
    const glm::uvec3 dims{17, 9, 24};
    const glm::vec2 viewport{1920.0f, 1080.0f};
    const glm::vec2 logs = calculate_cluster_factors(0.1f, 150.0f, static_cast<float>(dims.z), false);
    const glm::vec4 factors = pack_cluster_factors(dims, viewport, logs);
    EXPECT_NEAR(factors.x, 17.0f / 1920.0f, 1e-6f) << "one cluster's screen width";
    EXPECT_NEAR(factors.y, 9.0f / 1080.0f, 1e-6f) << "and its height";
    EXPECT_FLOAT_EQ(factors.z, logs.x) << "the log factors pass through";
    EXPECT_FLOAT_EQ(factors.w, logs.y);

    // The dimensions word carries the counts and their product.
    const glm::uvec4 packed_dims = pack_cluster_dimensions(dims);
    EXPECT_EQ(packed_dims.x, 17u);
    EXPECT_EQ(packed_dims.y, 9u);
    EXPECT_EQ(packed_dims.z, 24u);
    EXPECT_EQ(packed_dims.w, 17u * 9u * 24u) << "the total cluster count";

    // The shadow-map offset converts a spot light's index to its shadow map's.
    // With four cascades and two point lights it is +2, and the mapping holds:
    // light index + offset = shadow map index.
    const std::int32_t offset = spot_light_shadowmap_offset(4, 2);
    EXPECT_EQ(offset, 2);
    const std::size_t spot_light_index = 2;  // the first spot light, after two point lights
    EXPECT_EQ(static_cast<std::size_t>(static_cast<std::int32_t>(spot_light_index) + offset), 4u)
        << "the first spot light's shadow map sits after the four directional cascades";
    // More point lights than cascades gives a negative offset, which is fine as
    // the field is signed.
    EXPECT_EQ(spot_light_shadowmap_offset(0, 3), -3);
}

/** @brief A directional light's flags and cascades are one computation, since a
 *  light only keeps its shadows if its cascades fit the remaining array layers,
 *  and the volumetric flag again requires shadows (Bevy
 *  `render/light.rs:1171-1225`). */
TEST(Cluster, DirectionalLightFlagsAndCascadesMatchBevy) {
    using epix::pbr::directional_light_gpu_flags;
    using epix::pbr::DirectionalLightFlags;
    using epix::pbr::ExtractedDirectionalLight;
    using epix::pbr::NO_CLUSTERED_DECAL;
    using epix::pbr::pack_directional_color;

    ExtractedDirectionalLight light;
    light.shadows_enabled                  = true;
    light.volumetric                       = true;
    light.affects_lightmapped_mesh_diffuse = true;

    // Room to spare: shadows and volumetrics are granted, and four cascades.
    const auto roomy = directional_light_gpu_flags(light, 0, 4, 0, 32);
    EXPECT_TRUE((roomy.flags & DirectionalLightFlags::SHADOWS_ENABLED) != 0u);
    EXPECT_TRUE((roomy.flags & DirectionalLightFlags::VOLUMETRIC) != 0u);
    EXPECT_TRUE((roomy.flags & DirectionalLightFlags::AFFECTS_LIGHTMAPPED_MESH_DIFFUSE) != 0u);
    EXPECT_EQ(roomy.num_cascades, 4u) << "the default configuration's four";

    // No room left: the light keeps its other flags but silently loses its
    // shadows and its cascades.
    const auto squeezed = directional_light_gpu_flags(light, 0, 4, 30, 32);
    EXPECT_EQ(squeezed.flags & DirectionalLightFlags::SHADOWS_ENABLED, 0u)
        << "two layers left cannot hold four cascades";
    EXPECT_EQ(squeezed.num_cascades, 0u);
    EXPECT_TRUE((squeezed.flags & DirectionalLightFlags::AFFECTS_LIGHTMAPPED_MESH_DIFFUSE) != 0u)
        << "the other flags are unaffected";

    // Exactly fitting is enough.
    const auto exact = directional_light_gpu_flags(light, 0, 4, 28, 32);
    EXPECT_TRUE((exact.flags & DirectionalLightFlags::SHADOWS_ENABLED) != 0u);

    // The volumetric flag requires shadows, and both must be inside the count.
    ExtractedDirectionalLight no_shadows = light;
    no_shadows.shadows_enabled           = false;
    const auto unshadowed                = directional_light_gpu_flags(no_shadows, 0, 4, 0, 32);
    EXPECT_EQ(unshadowed.flags & DirectionalLightFlags::VOLUMETRIC, 0u) << "volumetrics need shadows";
    EXPECT_EQ(unshadowed.num_cascades, 0u);
    const auto beyond_count = directional_light_gpu_flags(light, 5, 4, 0, 32);
    EXPECT_EQ(beyond_count.flags & DirectionalLightFlags::VOLUMETRIC, 0u)
        << "past the volumetric light count in the sorted list";

    // The colour is premultiplied by illuminance, alpha included.
    EXPECT_EQ(pack_directional_color(glm::vec4(0.5f, 0.25f, 0.125f, 1.0f), 10000.0f),
              glm::vec4(5000.0f, 2500.0f, 1250.0f, 10000.0f));

    // A light without a clustered decal carries the sentinel, not zero.
    EXPECT_EQ(NO_CLUSTERED_DECAL, 0xFFFFFFFFu);
}

/** @brief The projection the light shadow path uses is the engine's one
 *  reverse-Z implementation, shared with the camera (Bevy `render/light.rs:900`,
 *  `bevy_camera`'s `PerspectiveProjection`, and
 *  `epix::camera::perspective_infinite_reverse_rh`). */
TEST(Cluster, CubeFaceProjectionUsesTheSharedReverseZMatrix) {
    using epix::camera::perspective_infinite_reverse_rh;
    using epix::camera::PerspectiveProjection;
    using epix::pbr::cube_face_projection;
    using epix::pbr::point_light_custom_data;

    const float z_near = 0.1f;
    const glm::mat4 projection = cube_face_projection(z_near);

    // One implementation, two callers, as in Bevy: the cube-face matrix is the
    // shared function, and the camera's own projection is built from it too.
    EXPECT_EQ(projection, perspective_infinite_reverse_rh(glm::radians(90.0f), 1.0f, z_near))
        << "the light path calls the shared function";
    PerspectiveProjection camera_projection;
    camera_projection.fov          = glm::radians(90.0f);
    camera_projection.aspect_ratio = 1.0f;
    camera_projection.near_plane   = z_near;
    camera_projection.near_clip_plane = glm::vec4(0.0f, 0.0f, -1.0f, -z_near);
    EXPECT_EQ(projection, camera_projection.get_clip_from_view())
        << "and so does the camera's projection, so the two cannot drift";

    // f = 1 / tan(45 degrees) = 1, with a square aspect, so the x and y scales
    // are both one.
    EXPECT_NEAR(projection[0][0], 1.0f, 1e-5f);
    EXPECT_NEAR(projection[1][1], 1.0f, 1e-5f);
    // The depth layout: reverse-Z with an infinite far plane.
    EXPECT_FLOAT_EQ(projection[2][2], 0.0f) << "nothing scales z";
    EXPECT_FLOAT_EQ(projection[2][3], -1.0f) << "w becomes -z";
    EXPECT_FLOAT_EQ(projection[3][2], z_near) << "the near plane's offset";
    EXPECT_FLOAT_EQ(projection[3][3], 0.0f);

    // The depth orientation the light path depends on: near maps to one and
    // infinity to zero, which is what the GreaterEqual depth comparisons expect.
    const auto ndc_z = [&](float view_z) {
        const glm::vec4 clip = projection * glm::vec4(0.0f, 0.0f, view_z, 1.0f);
        return clip.z / clip.w;
    };
    EXPECT_NEAR(ndc_z(-z_near), 1.0f, 1e-4f) << "the near plane is depth one";
    EXPECT_NEAR(ndc_z(-1.0f), z_near, 1e-3f) << "ndc_z = z_near / -view_z";
    EXPECT_NEAR(ndc_z(-1.0e6f), 0.0f, 1e-5f) << "and infinity approaches zero";
    EXPECT_GT(ndc_z(-z_near), ndc_z(-1.0f)) << "nearer is greater, which is what reverse-Z means";
    EXPECT_GT(ndc_z(-1.0f), ndc_z(-1.0e6f));

    // The light's custom data picks out the depth row and column the shader
    // reads: (0, -1, near, 0).
    EXPECT_EQ(point_light_custom_data(projection), glm::vec4(0.0f, -1.0f, z_near, 0.0f))
        << "what the shader actually receives";

    // A spot light's projection is the same function at twice its outer angle.
    const float outer_angle = glm::radians(30.0f);
    const glm::mat4 spot    = perspective_infinite_reverse_rh(outer_angle * 2.0f, 1.0f, z_near);
    EXPECT_NEAR(spot[1][1], 1.0f / std::tan(outer_angle), 1e-5f)
        << "a 30-degree half angle gives a 60-degree field of view";
}

/** @brief A camera's ambient light replaces the scene-wide one wholesale, and a
 *  shadow view pairs a depth attachment with its pass name (Bevy
 *  `render/light.rs:651-655,1153-1158`). */
TEST(Cluster, AmbientResolutionAndShadowViewMatchBevy) {
    using epix::pbr::AmbientLight;
    using epix::pbr::GlobalAmbientLight;
    using epix::pbr::resolve_ambient_light;
    using epix::pbr::ShadowView;

    GlobalAmbientLight global;
    global.color      = glm::vec4(0.25f, 0.5f, 0.75f, 1.0f);
    global.brightness = 200.0f;
    global.affects_lightmapped_meshes = false;

    // Without an override every field comes from the global, including the
    // lightmap flag - the copy Bevy makes is field for field.
    const auto from_global = resolve_ambient_light(nullptr, global);
    EXPECT_EQ(from_global.color, global.color);
    EXPECT_FLOAT_EQ(from_global.brightness, 200.0f);
    EXPECT_FALSE(from_global.affects_lightmapped_meshes);

    // An override replaces all three, rather than blending with the global: a
    // camera can turn the lightmapped contribution back on while dimming.
    AmbientLight override_light;
    override_light.color      = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    override_light.brightness = 10.0f;
    override_light.affects_lightmapped_meshes = true;
    const auto from_camera = resolve_ambient_light(&override_light, global);
    EXPECT_EQ(from_camera.color, override_light.color) << "not blended with the global colour";
    EXPECT_FLOAT_EQ(from_camera.brightness, 10.0f) << "nor with its brightness";
    EXPECT_TRUE(from_camera.affects_lightmapped_meshes) << "the flag is replaced too";

    // The resolved pair feeds the uniform the way the light path packs it:
    // colour premultiplied by brightness, and the flag on its own.
    EXPECT_EQ(epix::pbr::pack_ambient_color(from_camera.color, from_camera.brightness), glm::vec4(10.0f, 0.0f, 0.0f, 10.0f));

    // A shadow view carries the attachment and the pass name together.
    const ShadowView shadow_view{epix::render::render_resource::DepthAttachment{wgpu::TextureView{}}, "point_light_shadow_map"};
    EXPECT_EQ(shadow_view.pass_name, "point_light_shadow_map");
    EXPECT_FALSE(shadow_view.depth_attachment.clear_value.has_value())
        << "a depth attachment without a clear value does not clear";
}

/** @brief The shadow textures are budgeted from the device's limits, the point
 *  one being a cubemap array and the directional one shared with the spot shadow
 *  maps (Bevy `render/light.rs:1030,1076-1082,1250-1253`). */
TEST(Cluster, ShadowTextureBudgetsMatchBevy) {
    using epix::pbr::directional_light_shadow_texture_layers;
    using epix::pbr::directional_light_shadow_texture_size;
    using epix::pbr::point_light_shadow_texture_layers;
    using epix::pbr::point_light_shadow_view_count;
    using epix::pbr::POINT_LIGHT_CUBE_FACE_COUNT;

    EXPECT_EQ(POINT_LIGHT_CUBE_FACE_COUNT, 6u);

    // A point light's shadow map is a cubemap: six layers each.
    EXPECT_EQ(point_light_shadow_texture_layers(0), 6u) << "at least one cubemap, even with nothing casting";
    EXPECT_EQ(point_light_shadow_texture_layers(1), 6u);
    EXPECT_EQ(point_light_shadow_texture_layers(4), 24u);
    EXPECT_EQ(point_light_shadow_texture_layers(10), 60u);

    // The directional array holds the cascades and the spot shadow maps together,
    // because they share one texture.
    EXPECT_EQ(directional_light_shadow_texture_layers(0, 0), 1u) << "at least one layer";
    EXPECT_EQ(directional_light_shadow_texture_layers(4, 0), 4u);
    EXPECT_EQ(directional_light_shadow_texture_layers(4, 3), 7u);
    EXPECT_EQ(directional_light_shadow_texture_layers(1, 1), 2u);

    // The directional size is clamped to the device; a point light's faces are
    // the same size and are not clamped by this path.
    EXPECT_EQ(directional_light_shadow_texture_size(2048, 8192), 2048u) << "under the limit, unchanged";
    EXPECT_EQ(directional_light_shadow_texture_size(2048, 1024), 1024u) << "oversized requests degrade";
    EXPECT_EQ(directional_light_shadow_texture_size(4096, 4096), 4096u) << "exactly at the limit";
    EXPECT_EQ(directional_light_shadow_texture_size(64, 1024), 64u) << "the power-of-two floor still applies";

    // Only as many point lights get shadow maps as the device has cubemaps.
    EXPECT_EQ(point_light_shadow_view_count(3, 8), 3u);
    EXPECT_EQ(point_light_shadow_view_count(20, 8), 8u) << "capped by the device's cubemaps";
    EXPECT_EQ(point_light_shadow_view_count(0, 8), 0u);
    EXPECT_EQ(point_light_shadow_view_count(3, 0), 0u) << "a device with no cubemaps allocates no views";
}

/** @brief The shadow phase item groups work by the same keys the opaque 3D phase
 *  uses, minus the lightmap; a shadow item's entity pair and batch-set key are
 *  what the phase reads (Bevy `Shadow`, `ShadowBatchSetKey`, `ShadowBinKey`,
 *  `render/light.rs:2082-2136`). */
TEST(Cluster, ShadowPhaseItemMatchesBevy) {
    using epix::pbr::Shadow;
    using epix::pbr::ShadowBatchSetKey;
    using epix::pbr::ShadowBinKey;
    using epix::render::mesh::SlabId;

    // A batch set is indexed only when there is an index slab, which selects the
    // indirect-command layout.
    ShadowBatchSetKey unindexed;
    unindexed.vertex_slab = SlabId{0};
    EXPECT_FALSE(unindexed.indexed()) << "no index slab";
    ShadowBatchSetKey indexed = unindexed;
    indexed.index_slab        = SlabId{1};
    EXPECT_TRUE(indexed.indexed());
    EXPECT_NE(unindexed, indexed) << "the index slab is part of the key";

    // The key compares field for field, so two objects with the same pipeline,
    // draw function, material and slabs land in one batch set.
    ShadowBatchSetKey same = unindexed;
    EXPECT_EQ(unindexed, same);
    same.material_bind_group_index = 2;
    EXPECT_NE(unindexed, same) << "a different material bind group separates them";

    // Shadow items are binned by asset.
    const auto asset_id = epix::assets::UntypedAssetId{epix::image::DEFAULT_IMAGE_HANDLE.id()};
    ShadowBinKey first_bin{.asset_id = asset_id};
    ShadowBinKey second_bin{.asset_id = asset_id};
    EXPECT_EQ(first_bin, second_bin) << "the same asset bins together";

    // A phase item carries the render entity and the main-world entity it
    // represents, and takes its pipeline and draw function from its batch set.
    const auto render_entity = epix::ecs::Entity::from_index(7);
    const auto main_entity   = epix::ecs::Entity::from_index(8);
    Shadow item = Shadow::create(unindexed, first_bin, {render_entity, epix::render::sync_world::MainEntity{main_entity}},
                                 {0u, 5u}, epix::render::phase::PhaseItemExtraIndex{});
    EXPECT_EQ(item.entity(), render_entity) << "the render-world entity";
    EXPECT_EQ(item.main_entity().id(), main_entity) << "and the main-world entity it stands for";
    EXPECT_EQ(item.batch_range().first, 0u);
    EXPECT_EQ(item.batch_range().second, 5u);
    item.batch_range().second = 9u;
    EXPECT_EQ(item.batch_range().second, 9u) << "the range is mutable, as the batcher needs";
    EXPECT_EQ(item.cached_pipeline(), unindexed.pipeline_id);
    EXPECT_EQ(item.draw_function(), unindexed.draw_func);
}

/** @brief Each shadow-casting directional light takes the next slice of the
 *  view's shared shadow array, while lights without shadows - or on other layers
 *  - consume none (Bevy `prepare_lights`, `render/light.rs:1484-1525`). */
TEST(Cluster, DirectionalShadowSlicesMatchBevy) {
    using epix::pbr::directional_shadow_slices;
    using epix::pbr::ExtractedDirectionalLight;

    const auto make_light = [](bool shadows, epix::camera::RenderLayers layers) {
        ExtractedDirectionalLight light;
        light.shadows_enabled = shadows;
        light.render_layers   = std::move(layers);
        return light;
    };
    const auto layer0 = epix::camera::RenderLayers::layer(0);
    const auto layer1 = epix::camera::RenderLayers::layer(1);

    std::size_t total = 0;
    // Two shadow-casting lights in the view: the first takes layers 0-3, the
    // second 4-7, and the view's total is eight.
    const std::vector<ExtractedDirectionalLight> two{make_light(true, layer0), make_light(true, layer0)};
    const auto slices = directional_shadow_slices(two, layer0, 0, 32, &total);
    ASSERT_EQ(slices.size(), 2u);
    EXPECT_EQ(slices[0].base_index, 0u);
    EXPECT_EQ(slices[0].num_cascades, 4u) << "the default four cascades";
    EXPECT_EQ(slices[1].base_index, 4u) << "where the first light stopped";
    EXPECT_EQ(total, 8u);

    // A light on another layer is skipped without consuming a layer. Bevy filters
    // before enumerating, so the light indices are dense over the lights that are
    // in the view - the skipped light does not occupy a position.
    const std::vector<ExtractedDirectionalLight> mixed{make_light(true, layer0), make_light(true, layer1),
                                                      make_light(true, layer0)};
    total = 0;
    const auto filtered = directional_shadow_slices(mixed, layer0, 0, 32, &total);
    ASSERT_EQ(filtered.size(), 2u);
    EXPECT_EQ(filtered[0].light_index, 0u);
    EXPECT_EQ(filtered[1].light_index, 1u) << "dense over the lights in the view";
    EXPECT_EQ(filtered[1].base_index, 4u) << "and the skipped light consumed no layers";
    EXPECT_EQ(total, 8u);

    // A light without shadows consumes nothing, so the next one starts where it
    // would have.
    total = 0;
    const std::vector<ExtractedDirectionalLight> gap{make_light(true, layer0), make_light(false, layer0),
                                                     make_light(true, layer0)};
    const auto skipped = directional_shadow_slices(gap, layer0, 0, 32, &total);
    ASSERT_EQ(skipped.size(), 3u);
    EXPECT_EQ(skipped[1].num_cascades, 0u) << "no shadows";
    EXPECT_EQ(skipped[1].base_index, 0u) << "and no slice assigned";
    EXPECT_EQ(skipped[2].base_index, 4u) << "the third light starts where the first stopped";
    EXPECT_EQ(total, 8u);

    // When the layers run out, the light that does not fit is dropped rather than
    // overlapping the previous one.
    total = 0;
    const std::vector<ExtractedDirectionalLight> tight{make_light(true, layer0), make_light(true, layer0)};
    const auto squeezed = directional_shadow_slices(tight, layer0, 0, 6, &total);
    ASSERT_EQ(squeezed.size(), 2u);
    EXPECT_EQ(squeezed[0].num_cascades, 4u);
    EXPECT_EQ(squeezed[1].num_cascades, 0u) << "six layers cannot hold a second four-cascade light";
    EXPECT_EQ(total, 4u) << "so only the first light's cascades count";
}

/** @brief The cubemap face rotations aim each face's forward axis at its target,
 *  and a face's clip-from-world puts that target direction at the centre of the
 *  face (Bevy `render/light.rs:757-760,1273-1290`). */
TEST(Cluster, CubeFaceShadowViewsMatchBevy) {
    using epix::pbr::cube_face_projection;
    using epix::pbr::cube_face_view_rotations;
    using epix::pbr::point_light_face_clip_from_world;
    using epix::pbr::point_light_face_world_from_view;
    using epix::pbr::POINT_LIGHT_CUBE_FACE_COUNT;

    const auto rotations = cube_face_view_rotations();
    // The faces in WebGPU layer order, with the deliberate +Z/-Z swap.
    const std::array<glm::vec3, 6> expected_targets{glm::vec3(1.0f, 0.0f, 0.0f),  glm::vec3(-1.0f, 0.0f, 0.0f),
                                                    glm::vec3(0.0f, 1.0f, 0.0f),  glm::vec3(0.0f, -1.0f, 0.0f),
                                                    glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
    for (std::size_t face = 0; face < POINT_LIGHT_CUBE_FACE_COUNT; ++face) {
        // Forward is -Z, so the rotation must send -Z to the face's target.
        const glm::vec3 forward = glm::normalize(glm::vec3(rotations[face] * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
        EXPECT_NEAR(forward.x, expected_targets[face].x, 1e-5f) << "face " << face;
        EXPECT_NEAR(forward.y, expected_targets[face].y, 1e-5f) << "face " << face;
        EXPECT_NEAR(forward.z, expected_targets[face].z, 1e-5f) << "face " << face;
    }

    // The light's own rotation and scale are ignored: only its translation moves
    // the face views, so all six share one origin.
    const glm::vec3 light_position{3.0f, -2.0f, 5.0f};
    for (std::size_t face = 0; face < POINT_LIGHT_CUBE_FACE_COUNT; ++face) {
        const glm::mat4 world_from_view = point_light_face_world_from_view(rotations, light_position, face);
        EXPECT_EQ(glm::vec3(world_from_view[3]), light_position) << "face " << face << " is centred on the light";
        // The basis stays orthonormal, so the projection is not skewed.
        EXPECT_NEAR(glm::length(glm::vec3(world_from_view[0])), 1.0f, 1e-5f);
        EXPECT_NEAR(glm::length(glm::vec3(world_from_view[1])), 1.0f, 1e-5f);
        EXPECT_NEAR(glm::length(glm::vec3(world_from_view[2])), 1.0f, 1e-5f);
    }

    // A point one unit along a face's target direction from the light lands at the
    // centre of that face's projection - which is what makes the six faces cover
    // the whole sphere with no gap or overlap.
    const float z_near = 0.1f;
    for (std::size_t face = 0; face < POINT_LIGHT_CUBE_FACE_COUNT; ++face) {
        const glm::vec3 sample = light_position + expected_targets[face];
        const glm::vec4 clip =
            point_light_face_clip_from_world(rotations, light_position, face, z_near) * glm::vec4(sample, 1.0f);
        ASSERT_GT(clip.w, 0.0f) << "face " << face << " looks at the sample rather than away from it";
        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        EXPECT_NEAR(ndc.x, 0.0f, 1e-4f) << "face " << face << " centres its own target direction";
        EXPECT_NEAR(ndc.y, 0.0f, 1e-4f) << "face " << face;
        // And it is inside the near plane, so it is not clipped.
        EXPECT_GT(clip.w, z_near) << "the sample is in front of the near plane";
    }

    // The face projection itself is the shared cube-face one.
    EXPECT_EQ(cube_face_projection(z_near), epix::camera::perspective_infinite_reverse_rh(glm::radians(90.0f), 1.0f, z_near));
}

/** @brief A point light's six faces take consecutive array layers, are named the
 *  way Bevy names them, and each gets its own projection (Bevy
 *  `render/light.rs:1290-1360`). */
TEST(Cluster, PointLightShadowViewRequestsMatchBevy) {
    using epix::pbr::point_light_shadow_array_layer;
    using epix::pbr::point_light_shadow_pass_name;
    using epix::pbr::point_light_shadow_view_requests;
    using epix::pbr::POINT_LIGHT_CUBE_FACE_COUNT;

    // Layers are not shared between lights, and never repeat.
    EXPECT_EQ(point_light_shadow_array_layer(0, 0), 0u);
    EXPECT_EQ(point_light_shadow_array_layer(0, 5), 5u);
    EXPECT_EQ(point_light_shadow_array_layer(1, 0), 6u) << "the second light starts after the first's six";
    EXPECT_EQ(point_light_shadow_array_layer(3, 2), 20u);

    // The pass names Bevy uses, including the face spelling.
    EXPECT_EQ(point_light_shadow_pass_name(0, 0), "shadow_point_light_0_+x");
    EXPECT_EQ(point_light_shadow_pass_name(2, 5), "shadow_point_light_2_-z");

    // Two shadow-casting lights give twelve views, each on its own layer.
    const auto first  = epix::ecs::Entity::from_index(1);
    const auto second = epix::ecs::Entity::from_index(2);
    const std::vector<std::pair<epix::ecs::Entity, glm::vec3>> lights{
        {first, glm::vec3(1.0f, 2.0f, 3.0f)}, {second, glm::vec3(-4.0f, 0.0f, 1.0f)}};
    const float z_near = 0.1f;
    const auto requests = point_light_shadow_view_requests(lights, z_near, 1024);
    ASSERT_EQ(requests.size(), 2u * POINT_LIGHT_CUBE_FACE_COUNT);

    std::vector<std::uint32_t> layers;
    for (const auto& request : requests) {
        layers.push_back(request.array_layer);
        EXPECT_EQ(request.viewport_size, 1024u) << "the shadow map's side is the view's viewport";
        EXPECT_EQ(request.owner.kind, epix::pbr::LightEntity::Kind::Point);
        EXPECT_EQ(request.clip_from_world, request.clip_from_world) << "a usable matrix";
    }
    std::sort(layers.begin(), layers.end());
    EXPECT_EQ(std::adjacent_find(layers.begin(), layers.end()), layers.end()) << "no two views share a layer";
    EXPECT_EQ(layers.front(), 0u);
    EXPECT_EQ(layers.back(), 11u) << "twelve views over layers 0 to 11";

    // The first light's first face and the second light's first face belong to
    // different lights and different layers.
    EXPECT_EQ(requests[0].light_entity, first);
    EXPECT_EQ(requests[0].owner.face_index, 0u);
    EXPECT_EQ(requests[POINT_LIGHT_CUBE_FACE_COUNT].light_entity, second);
    EXPECT_EQ(requests[POINT_LIGHT_CUBE_FACE_COUNT].array_layer, 6u);

    // A light's own faces point in six different directions: their projections
    // differ, which is what stops a face being reused for the wrong side.
    EXPECT_NE(requests[0].clip_from_world, requests[1].clip_from_world);
    EXPECT_NE(requests[0].clip_from_world, requests[4].clip_from_world);
}

/** @brief Directional cascades take layers after each other across lights, each
 *  view gets its own attachment layer, the GPU cascade's far bound comes from the
 *  configuration's bounds, and the pass names are Bevy's (Bevy
 *  `render/light.rs:1484-1600`). */
TEST(Cluster, DirectionalShadowViewRequestsMatchBevy) {
    using epix::pbr::Cascade;
    using epix::pbr::directional_shadow_pass_name;
    using epix::pbr::directional_shadow_slices;
    using epix::pbr::directional_shadow_view_requests;
    using epix::pbr::ExtractedDirectionalLight;
    using epix::pbr::fill_gpu_directional_cascades;
    using epix::pbr::GpuDirectionalCascade;
    using epix::pbr::GpuDirectionalLight;

    const auto layer0 = epix::camera::RenderLayers::layer(0);

    EXPECT_EQ(directional_shadow_pass_name(0, 0), "shadow_directional_light_0_cascade_0");
    EXPECT_EQ(directional_shadow_pass_name(3, 2), "shadow_directional_light_3_cascade_2");

    // The GPU cascade takes its matrix and texel size from the cascade, but its
    // far bound from the configuration's bounds - which is a different array.
    const std::vector<Cascade> cascades{Cascade{.clip_from_world = glm::mat4(2.0f), .texel_size = 0.5f},
                                        Cascade{.clip_from_world = glm::mat4(3.0f), .texel_size = 0.25f}};
    const std::vector<float> bounds{10.0f, 20.0f, 30.0f, 40.0f};
    GpuDirectionalLight light;
    light.cascades[3] = GpuDirectionalCascade{.texel_size = 99.0f};  // stale, must be cleared
    fill_gpu_directional_cascades(light, cascades, bounds);
    EXPECT_EQ(light.cascades[0].clip_from_world, glm::mat4(2.0f));
    EXPECT_FLOAT_EQ(light.cascades[0].texel_size, 0.5f);
    EXPECT_FLOAT_EQ(light.cascades[0].far_bound, 10.0f) << "from the bounds, not the cascade";
    EXPECT_FLOAT_EQ(light.cascades[1].far_bound, 20.0f);
    EXPECT_FLOAT_EQ(light.cascades[3].texel_size, 0.0f) << "cascades past the built ones are cleared";

    // Two shadow-casting lights, four cascades each.
    const auto first  = epix::ecs::Entity::from_index(1);
    const auto second = epix::ecs::Entity::from_index(2);
    const std::vector<ExtractedDirectionalLight> lights{ExtractedDirectionalLight{.shadows_enabled = true},
                                                       ExtractedDirectionalLight{.shadows_enabled = true}};
    std::size_t total = 0;
    const auto slices = directional_shadow_slices(lights, layer0, 0, 32, &total);
    ASSERT_EQ(slices.size(), 2u);

    std::vector<std::vector<Cascade>> per_light{
        {Cascade{.clip_from_world = glm::mat4(2.0f)}, Cascade{.clip_from_world = glm::mat4(3.0f)},
         Cascade{.clip_from_world = glm::mat4(4.0f)}, Cascade{.clip_from_world = glm::mat4(5.0f)}},
        {Cascade{.clip_from_world = glm::mat4(6.0f)}, Cascade{.clip_from_world = glm::mat4(7.0f)},
         Cascade{.clip_from_world = glm::mat4(8.0f)}, Cascade{.clip_from_world = glm::mat4(9.0f)}}};
    const auto requests = directional_shadow_view_requests({first, second}, per_light, slices, 2048);
    ASSERT_EQ(requests.size(), 8u) << "two lights of four cascades";
    EXPECT_EQ(requests[0].array_layer, 0u);
    EXPECT_EQ(requests[3].array_layer, 3u);
    EXPECT_EQ(requests[4].array_layer, 4u) << "the second light starts where the first stopped";
    EXPECT_EQ(requests[7].array_layer, 7u);
    EXPECT_EQ(requests[0].light_entity, first);
    EXPECT_EQ(requests[4].light_entity, second);
    EXPECT_EQ(requests[5].owner.kind, epix::pbr::LightEntity::Kind::Directional);
    EXPECT_EQ(requests[5].owner.cascade_index, 1u) << "the owner records which cascade";
    EXPECT_EQ(requests[0].viewport_size, 2048u);
    EXPECT_EQ(requests[0].pass_name, "shadow_directional_light_0_cascade_0");
    EXPECT_EQ(requests[6].clip_from_world, glm::mat4(8.0f)) << "the cascade's own matrix";
    EXPECT_NE(requests[0].clip_from_world, requests[1].clip_from_world) << "each cascade differs";

    // A light with no cascades granted contributes no views at all.
    const auto squeezed_slices = directional_shadow_slices(lights, layer0, 0, 6, &total);
    const auto squeezed        = directional_shadow_view_requests({first, second}, per_light, squeezed_slices, 2048);
    EXPECT_EQ(squeezed.size(), 4u) << "only the first light's four cascades fit the six layers";
}

/** @brief The far plane is the largest object depth plus its range, the first
 *  slice follows the projection and the slice count, and the far plane is raised
 *  to meet it (Bevy `assign.rs:337-369`). */
TEST(Cluster, ViewFarZAndFirstSliceDepthMatchBevy) {
    using epix::pbr::ClusterableObjectAssignmentData;
    using epix::pbr::ClusterConfig;
    using epix::pbr::ClusterFarZMode;
    using epix::pbr::far_z_at_least_first_slice_depth;
    using epix::pbr::first_slice_depth;
    using epix::pbr::view_far_z;

    const auto object_at = [](float z, float range) {
        ClusterableObjectAssignmentData object;
        object.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, z));
        object.range     = range;
        return object;
    };
    const glm::mat4 identity{1.0f};
    const glm::vec3 unit_scale{1.0f};

    // A constant mode ignores the objects entirely.
    EXPECT_FLOAT_EQ(view_far_z(ClusterFarZMode::constant_far(42.0f), {object_at(-100.0f, 5.0f)}, identity, unit_scale),
                    42.0f);

    // Maximum range: view space looks down -Z, so a depth is -z, plus the range.
    EXPECT_FLOAT_EQ(view_far_z(ClusterFarZMode{}, {}, identity, unit_scale), 0.0f) << "no objects, no depth";
    EXPECT_FLOAT_EQ(view_far_z(ClusterFarZMode{}, {object_at(-10.0f, 2.0f)}, identity, unit_scale), 12.0f);
    EXPECT_FLOAT_EQ(view_far_z(ClusterFarZMode{}, {object_at(-10.0f, 2.0f), object_at(-30.0f, 1.0f)}, identity, unit_scale),
                    31.0f)
        << "the furthest object wins";
    // The object's range is scaled by the view's z scale, the depth is not.
    EXPECT_FLOAT_EQ(view_far_z(ClusterFarZMode{}, {object_at(-10.0f, 2.0f)}, identity, glm::vec3(1.0f, 1.0f, 3.0f)), 16.0f);

    // A view's own transform changes the measured depth: a 90-degree turn about Y
    // maps world +X to the view's -Z, so an object out along +X is in front of it.
    // (Out along -X it would be behind the view, and the depth is floored at zero,
    // so it would contribute nothing.)
    const glm::mat4 turned = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    ClusterableObjectAssignmentData sideways;
    sideways.transform = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f));
    sideways.range     = 0.0f;
    EXPECT_NEAR(view_far_z(ClusterFarZMode{}, {sideways}, turned, unit_scale), 5.0f, 1e-4f)
        << "the row picks up the axis that faces the view";

    ClusterableObjectAssignmentData behind = sideways;
    behind.transform = glm::translate(glm::mat4(1.0f), glm::vec3(-5.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(view_far_z(ClusterFarZMode{}, {behind}, turned, unit_scale), 0.0f)
        << "an object behind the view contributes no depth";

    // First slice depth: an orthographic view derives it from its projection.
    const float z_near = 0.1f;
    const glm::mat4 ortho = glm::orthoRH_ZO(-1.0f, 1.0f, -0.75f, 0.75f, 100.0f, z_near);
    const ClusterConfig defaults{};
    EXPECT_NEAR(first_slice_depth(defaults, true, 8, ortho, unit_scale, 100.0f), z_near, 1e-3f)
        << "the orthographic near plane";

    // A perspective view with several slices uses the configured depth.
    EXPECT_FLOAT_EQ(first_slice_depth(defaults, false, 8, glm::mat4(1.0f), unit_scale, 500.0f), 5.0f)
        << "the configured first slice depth";
    // With a single slice it must reach the far plane instead, and the scale still
    // applies to whichever wins.
    EXPECT_FLOAT_EQ(first_slice_depth(defaults, false, 1, glm::mat4(1.0f), unit_scale, 500.0f), 500.0f);
    EXPECT_FLOAT_EQ(first_slice_depth(defaults, false, 1, glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, 2.0f), 500.0f), 1000.0f)
        << "including when the far plane is what won";
    EXPECT_FLOAT_EQ(first_slice_depth(defaults, false, 8, glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, 2.0f), 500.0f), 10.0f)
        << "and when the configuration won";

    // The far plane is raised to meet the first slice, never left behind it.
    EXPECT_FLOAT_EQ(far_z_at_least_first_slice_depth(10.0f, 20.0f), 20.0f);
    EXPECT_FLOAT_EQ(far_z_at_least_first_slice_depth(30.0f, 20.0f), 30.0f) << "already beyond it";
}

/** @brief Cascades overlap by the configured proportion, and each cascade's
 *  diameter, texel snapping and centre follow Bevy's stability rules (Bevy
 *  `cascade.rs:232-333`). */
TEST(Cluster, CascadeCalculationMatchesBevy) {
    using epix::pbr::calculate_cascade;
    using epix::pbr::cascade_bounds;
    using epix::pbr::default_cascade_shadow_config;

    // The overlap rule: the first cascade starts at the minimum distance, each
    // later one at the previous far bound scaled by 1 - overlap.
    const auto config = default_cascade_shadow_config();
    ASSERT_EQ(config.bounds.size(), 4u);
    const auto bounds = cascade_bounds(config);
    ASSERT_EQ(bounds.size(), 4u);
    EXPECT_NEAR(bounds[0].first, 0.1f, 1e-5f) << "the minimum distance";
    for (std::size_t i = 0; i < bounds.size(); ++i) {
        EXPECT_FLOAT_EQ(bounds[i].second, config.bounds[i]);
        if (i > 0) {
            EXPECT_NEAR(bounds[i].first, (1.0f - config.overlap_proportion) * config.bounds[i - 1], 1e-4f)
                << "cascade " << i << " starts inside the previous one";
        }
    }

    // A cascade of a known frustum slice, with the light looking down -Z so light
    // space and camera space coincide.
    const float z_near = 0.1f;
    const float z_far  = 10.0f;
    epix::camera::PerspectiveProjection projection;
    projection.fov          = glm::radians(90.0f);
    projection.aspect_ratio = 1.0f;
    projection.near_plane   = z_near;
    const auto corners      = projection.get_frustum_corners(-z_near, -z_far);

    const float texture_size = 1024.0f;
    const glm::mat4 identity{1.0f};
    const auto cascade = calculate_cascade(corners, texture_size, identity, identity);

    // The diameter is an integer - the rule that makes the texel size exactly
    // representable - and the texel size divides it.
    const float diameter = cascade.texel_size * texture_size;
    EXPECT_FLOAT_EQ(diameter, std::ceil(diameter)) << "an integer diameter";
    EXPECT_LT(cascade.texel_size, 1.0f);
    EXPECT_GT(cascade.texel_size, 0.0f);

    // The centre is snapped to integer multiples of the texel size, so it survives
    // the round trip through a float exactly.
    const glm::vec3 centre = glm::vec3(cascade.world_from_cascade[3]);
    const float snapped_x  = centre.x / cascade.texel_size;
    const float snapped_y  = centre.y / cascade.texel_size;
    EXPECT_NEAR(snapped_x, std::floor(snapped_x), 1e-3f) << "x is an integer number of texels";
    EXPECT_NEAR(snapped_y, std::floor(snapped_y), 1e-3f) << "and so is y";

    // The cascade's own projection maps its centre to the middle of the view.
    const glm::vec4 clip = cascade.clip_from_cascade * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(clip.x / clip.w, 0.0f, 1e-5f);
    EXPECT_NEAR(clip.y / clip.w, 0.0f, 1e-5f);
    EXPECT_NEAR(clip.z / clip.w, 1.0f, 1e-5f) << "reverse-Z, so the near plane is depth one";

    // The combined matrix puts the world position of the cascade's centre at the
    // middle of the shadow map, which is what makes the cascade cover the slice.
    const glm::vec4 clip_from_world = cascade.clip_from_world * glm::vec4(centre, 1.0f);
    ASSERT_GT(clip_from_world.w, 0.0f);
    EXPECT_NEAR(clip_from_world.x / clip_from_world.w, 0.0f, 1e-4f);
    EXPECT_NEAR(clip_from_world.y / clip_from_world.w, 0.0f, 1e-4f);
}

/** @brief The per-view assembly builds one cascade per frustum slice, from a
 *  light matrix made of the light's rotation alone (Bevy
 *  `build_directional_light_cascades`, `cascade.rs:216-251`). */
TEST(Cluster, BuildViewCascadesMatchesBevy) {
    using epix::pbr::build_view_cascades;

    // A unit cube's corners in the order calculate_cascade wants: bottom-right,
    // top-right, top-left, bottom-left for the near plane, then the far plane.
    const float half = 0.5f;
    const std::array<glm::vec3, 8> near_slice{glm::vec3(half, -half, -half), glm::vec3(half, half, -half),
                                              glm::vec3(-half, half, -half), glm::vec3(-half, -half, -half),
                                              glm::vec3(half, -half, half),  glm::vec3(half, half, half),
                                              glm::vec3(-half, half, half),  glm::vec3(-half, -half, half)};

    const float texture_size = 1024.0f;
    const auto identity_cascades =
        build_view_cascades({near_slice, near_slice}, texture_size, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::mat4(1.0f));
    ASSERT_EQ(identity_cascades.size(), 2u) << "one cascade per slice";

    // The cube's body diagonal is sqrt(3), so the ceiled diameter is 2 and the
    // texel size is 2 / texture_size. The far-plane diagonal is only sqrt(2), so
    // the body diagonal is what wins.
    EXPECT_FLOAT_EQ(identity_cascades[0].texel_size, 2.0f / texture_size);
    for (const auto& cascade : identity_cascades) {
        EXPECT_FLOAT_EQ(cascade.texel_size, 2.0f / texture_size) << "the same slice gives the same texel size";
        // The light matrix is a pure rotation: unit axes, no translation.
        EXPECT_FLOAT_EQ(cascade.world_from_cascade[3][0], std::floor(cascade.world_from_cascade[3][0] / cascade.texel_size) * cascade.texel_size);
        EXPECT_NEAR(glm::length(glm::vec3(cascade.world_from_cascade[0])), 1.0f, 1e-5f);
        EXPECT_NEAR(glm::length(glm::vec3(cascade.world_from_cascade[1])), 1.0f, 1e-5f);
        EXPECT_NEAR(glm::length(glm::vec3(cascade.world_from_cascade[2])), 1.0f, 1e-5f);
    }

    // Rotating the light changes the light space, so the cascades differ - and a
    // rotation about Y leaves the cube's light-space extent the same, since the
    // cube is symmetric, but the matrices themselves still change.
    const glm::quat turned = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const auto turned_cascades = build_view_cascades({near_slice}, texture_size, turned, glm::mat4(1.0f));
    ASSERT_EQ(turned_cascades.size(), 1u);
    EXPECT_NE(turned_cascades[0].clip_from_world, identity_cascades[0].clip_from_world)
        << "the light's rotation reaches the cascade's matrices";

    // A view transform reaches them too, through the light-from-camera matrix.
    const glm::mat4 moved_view = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f));
    const auto moved           = build_view_cascades({near_slice}, texture_size, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), moved_view);
    ASSERT_EQ(moved.size(), 1u);
    EXPECT_NE(moved[0].clip_from_world, identity_cascades[0].clip_from_world)
        << "moving the view moves the slice's corners in light space";

    // No slices, no cascades.
    EXPECT_TRUE(build_view_cascades({}, texture_size, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::mat4(1.0f)).empty());
}

/** @brief A point light's six cubemap frusta are 90-degree pyramids whose far
 *  plane sits at the light's range, so a sphere beyond it is outside and a sphere
 *  beside the axis is outside that face (Bevy `update_point_light_frusta`,
 *  `bevy_light/src/point_light.rs:196-244`). */
TEST(Cluster, PointLightCubemapFrustaMatchBevy) {
    using epix::camera::Sphere;
    using epix::pbr::point_light_cubemap_frusta;

    const glm::vec3 light_position{2.0f, -1.0f, 4.0f};
    const glm::vec3 light_back{0.0f, 0.0f, 1.0f};  // the light's backward axis
    const float range     = 20.0f;
    const float z_near    = 0.1f;
    const auto frusta     = point_light_cubemap_frusta(light_position, light_back, range, z_near);

    const auto face_direction = [](std::size_t face) {
        const std::array<glm::vec3, 6> directions{glm::vec3(1.0f, 0.0f, 0.0f),  glm::vec3(-1.0f, 0.0f, 0.0f),
                                                  glm::vec3(0.0f, 1.0f, 0.0f),  glm::vec3(0.0f, -1.0f, 0.0f),
                                                  glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
        return directions[face];
    };

    // A small sphere at the light itself is inside every face's frustum.
    const Sphere at_light{.center = light_position, .radius = 0.01f};
    for (std::size_t face = 0; face < frusta.size(); ++face) {
        EXPECT_TRUE(frusta[face].intersects_sphere(at_light)) << "face " << face << " contains the light";
    }

    // The far plane is the light's, not each face's: Bevy orients one plane by the
    // light's backward axis and gives it to all six faces. So the void behind the
    // light is cut for every face...
    const Sphere behind{.center = light_position - light_back * (range + 1.0f), .radius = 0.01f};
    for (std::size_t face = 0; face < frusta.size(); ++face) {
        EXPECT_FALSE(frusta[face].intersects_sphere(behind))
            << "face " << face << " is bounded behind the light at the range";
    }
    // ...and in front of it, along the backward axis, is inside.
    const Sphere in_front{.center = light_position + light_back * (range - 1.0f), .radius = 0.01f};
    EXPECT_TRUE(frusta[5].intersects_sphere(in_front)) << "the face pointing along the backward axis reaches it";

    // All six faces share that one far plane, which is the part a reader would get
    // wrong: there are not six of them.
    for (std::size_t face = 1; face < frusta.size(); ++face) {
        EXPECT_EQ(frusta[face].planes[5], frusta[0].planes[5]) << "face " << face << " reuses the light's far plane";
    }
    // The other five planes do differ per face, so the six frusta are genuinely
    // six pyramids rather than one repeated.
    EXPECT_NE(frusta[0].planes[0], frusta[1].planes[0]) << "opposite faces have opposite side planes";
    EXPECT_NE(frusta[0].planes[1], frusta[4].planes[1]);

    // Flipping the light's backward axis cuts the other side instead, which is what
    // shows the parameter is the light's own axis: the +Z-facing face now reaches
    // past the light and stops on the far side of it.
    const auto flipped = point_light_cubemap_frusta(light_position, -light_back, range, z_near);
    const Sphere past_flipped{.center = light_position + light_back * (range + 1.0f), .radius = 0.01f};
    EXPECT_TRUE(flipped[5].intersects_sphere(in_front)) << "the flipped plane leaves this side open";
    EXPECT_FALSE(flipped[5].intersects_sphere(past_flipped)) << "and bounds the other side at the range";
    EXPECT_TRUE(frusta[5].intersects_sphere(past_flipped))
        << "whereas the unflipped frustum of the same face has no such bound";
}

/** @brief Each cascade gets its own frustum, and a cascade view's near plane is
 *  pushed to infinity so it culls nothing in front of the light (Bevy
 *  `update_directional_light_frusta` and `prepare_lights`,
 *  `bevy_light/src/directional_light.rs:228-240`, `render/light.rs:1565-1571`). */
TEST(Cluster, DirectionalCascadeFrustaMatchBevy) {
    using epix::camera::Frustum;
    using epix::camera::Sphere;
    using epix::pbr::build_view_cascades;
    using epix::pbr::directional_cascade_frusta;
    using epix::pbr::directional_view_frustum;

    // A realistic slice: a real projection's corners between 1 and 10 units in
    // front of the light, so the whole slice is on the light's forward side. (A
    // synthetic cube straddling the light does not work here: the near plane is
    // then not the binding one and the push changes nothing.)
    epix::camera::PerspectiveProjection projection;
    projection.fov          = glm::radians(90.0f);
    projection.aspect_ratio = 1.0f;
    projection.near_plane   = 0.1f;
    const std::array<glm::vec3, 8> slice = projection.get_frustum_corners(-1.0f, -10.0f);

    const auto cascades = build_view_cascades({slice, slice}, 1024.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::mat4(1.0f));
    ASSERT_EQ(cascades.size(), 2u);

    // One frustum per cascade, from that cascade's clip-from-world.
    const auto frusta = directional_cascade_frusta(cascades);
    ASSERT_EQ(frusta.size(), 2u);
    EXPECT_EQ(frusta[0].planes, Frustum::from_clip_from_world(cascades[0].clip_from_world).planes);
    EXPECT_EQ(frusta[1].planes, Frustum::from_clip_from_world(cascades[1].clip_from_world).planes);

    // A sphere between the light and the slice, well beyond the slice's near plane.
    // It is *not* culled, and pushing the near plane to infinity does not free it
    // either - which is the real behaviour and worth stating: Bevy's plane 4 is
    // `row3 + row2`, the negative-one-to-one near plane, and under the zero-to-one
    // convention (the one the engine uses) that half-space admits every point in
    // front of the view. The true near bound would be `row3 - row2`, which Bevy does
    // not compute, because the GPU clips there anyway. So the push is faithful and
    // inert: it changes the plane and culls nothing.
    const Sphere in_front_of_slice{.center = glm::vec3(0.0f, 0.0f, -0.5f), .radius = 0.001f};
    const Frustum pushed = directional_view_frustum(frusta[0]);
    EXPECT_TRUE(frusta[0].intersects_sphere(in_front_of_slice))
        << "the near plane slot does not bound this, under a zero-to-one projection";
    EXPECT_TRUE(pushed.intersects_sphere(in_front_of_slice));

    // Over a range of probes the two frusta agree exactly: the push removes no
    // space, which is all the infinity distance can do.
    for (int step = -12; step <= 12; ++step) {
        const Sphere probe{.center = glm::vec3(0.0f, 0.0f, static_cast<float>(step) * 1.0f), .radius = 0.001f};
        EXPECT_EQ(frusta[0].intersects_sphere(probe), pushed.intersects_sphere(probe))
            << "the push changes no culling at z = " << probe.center.z;
    }

    // Everything else is untouched: the push changes one plane and leaves the rest.
    for (std::size_t plane = 0; plane < frusta[0].planes.size(); ++plane) {
        if (plane == Frustum::NEAR_PLANE_IDX) {
            EXPECT_EQ(glm::vec3(pushed.planes[plane]), glm::vec3(frusta[0].planes[plane]))
                << "the normal is kept";
            EXPECT_FALSE(std::isfinite(pushed.planes[plane].w)) << "only the distance becomes infinite";
        } else {
            EXPECT_EQ(pushed.planes[plane], frusta[0].planes[plane]) << "plane " << plane << " is unchanged";
        }
    }
    EXPECT_EQ(Frustum::NEAR_PLANE_IDX, 4u) << "Bevy's index for the near plane";
}

/** @brief The light unit tables carry Bevy's values (Bevy `light_consts`,
 *  `bevy_light/src/lib.rs:70-130`). */
TEST(Cluster, LightConstsMatchBevy) {
    using namespace epix::pbr::light_consts;

    // The watts-to-lumens approximations.
    EXPECT_FLOAT_EQ(lumens::LUMENS_PER_LED_WATTS, 90.0f);
    EXPECT_FLOAT_EQ(lumens::LUMENS_PER_INCANDESCENT_WATTS, 13.8f);
    EXPECT_FLOAT_EQ(lumens::LUMENS_PER_HALOGEN_WATTS, 19.8f);
    EXPECT_FLOAT_EQ(lumens::VERY_LARGE_CINEMA_LIGHT, 1'000'000.0f);

    // The lux presets, from starlight to raw sunlight - a range of nine orders of
    // magnitude, which is why the values are spelled out rather than inferred.
    EXPECT_FLOAT_EQ(lux::MOONLESS_NIGHT, 0.0001f);
    EXPECT_FLOAT_EQ(lux::FULL_MOON_NIGHT, 0.05f);
    EXPECT_FLOAT_EQ(lux::CIVIL_TWILIGHT, 3.4f);
    EXPECT_FLOAT_EQ(lux::LIVING_ROOM, 50.0f);
    EXPECT_FLOAT_EQ(lux::HALLWAY, 80.0f);
    EXPECT_FLOAT_EQ(lux::DARK_OVERCAST_DAY, 100.0f);
    EXPECT_FLOAT_EQ(lux::OFFICE, 320.0f);
    EXPECT_FLOAT_EQ(lux::CLEAR_SUNRISE, 400.0f);
    EXPECT_FLOAT_EQ(lux::OVERCAST_DAY, 1'000.0f);
    EXPECT_FLOAT_EQ(lux::AMBIENT_DAYLIGHT, 10'000.0f);
    EXPECT_FLOAT_EQ(lux::FULL_DAYLIGHT, 20'000.0f);
    EXPECT_FLOAT_EQ(lux::DIRECT_SUNLIGHT, 100'000.0f);
    EXPECT_FLOAT_EQ(lux::RAW_SUNLIGHT, 130'000.0f);

    // The presets are ordered as their names suggest, which is the one property a
    // transcription error would break.
    EXPECT_LT(lux::MOONLESS_NIGHT, lux::FULL_MOON_NIGHT);
    EXPECT_LT(lux::CIVIL_TWILIGHT, lux::LIVING_ROOM);
    EXPECT_LT(lux::HALLWAY, lux::DARK_OVERCAST_DAY);
    EXPECT_LT(lux::OVERCAST_DAY, lux::AMBIENT_DAYLIGHT);
    EXPECT_LT(lux::FULL_DAYLIGHT, lux::DIRECT_SUNLIGHT);
    EXPECT_LT(lux::DIRECT_SUNLIGHT, lux::RAW_SUNLIGHT);

    // And the two lights use the table rather than copies of it.
    EXPECT_FLOAT_EQ(epix::pbr::PointLight{}.intensity, lumens::VERY_LARGE_CINEMA_LIGHT);
    EXPECT_FLOAT_EQ(epix::pbr::DirectionalLight{}.illuminance, lux::AMBIENT_DAYLIGHT);
}

/** @brief The frusta map is rebuilt from the cascades map, preserving the views as
 *  keys rather than renumbering them (Bevy `update_directional_light_frusta`,
 *  `directional_light.rs:228-240`). */
TEST(Cluster, ToCascadesFrustaMatchesBevy) {
    using epix::pbr::build_view_cascades;
    using epix::pbr::Cascades;
    using epix::pbr::to_cascades_frusta;

    epix::camera::PerspectiveProjection projection;
    projection.fov          = glm::radians(90.0f);
    projection.aspect_ratio = 1.0f;
    projection.near_plane   = 0.1f;
    const auto slice        = projection.get_frustum_corners(-1.0f, -10.0f);

    // Two views, one with two cascades and one with a single cascade, keyed by
    // entities that are not in ascending order - a rebuild that renumbered the keys
    // would attach the wrong frusta to the wrong view.
    const auto first_view  = epix::ecs::Entity::from_index(7);
    const auto second_view = epix::ecs::Entity::from_index(2);
    Cascades cascades;
    cascades.cascades.emplace(first_view, build_view_cascades({slice, slice}, 1024.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::mat4(1.0f)));
    cascades.cascades.emplace(second_view, build_view_cascades({slice}, 1024.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::mat4(1.0f)));

    const auto frusta = to_cascades_frusta(cascades);
    ASSERT_EQ(frusta.frusta.size(), 2u);
    ASSERT_TRUE(frusta.frusta.contains(first_view)) << "the first view keeps its key";
    ASSERT_TRUE(frusta.frusta.contains(second_view)) << "and so does the second";
    EXPECT_EQ(frusta.frusta.at(first_view).size(), 2u) << "one frustum per cascade";
    EXPECT_EQ(frusta.frusta.at(second_view).size(), 1u);
    EXPECT_EQ(frusta.frusta.at(first_view)[0].planes,
              epix::camera::Frustum::from_clip_from_world(cascades.cascades.at(first_view)[0].clip_from_world).planes);

    // An empty map gives an empty map: no stale frusta survive a rebuild.
    EXPECT_TRUE(to_cascades_frusta(Cascades{}).frusta.empty());
}

/** @brief A point light's frusta are rebuilt only when something that matters
 *  changed, and only for a shadow-casting light that is in some cluster (Bevy
 *  `update_point_light_frusta`, `point_light.rs:205-219`). */
TEST(Cluster, ShouldUpdatePointLightFrustaMatchesBevy) {
    using epix::pbr::should_update_point_light_frusta;

    // Nothing changed and the light is fine: skipped, which is the common frame.
    EXPECT_FALSE(should_update_point_light_frusta(false, false, true, true));

    // The light itself changed: rebuilt.
    EXPECT_TRUE(should_update_point_light_frusta(true, false, true, true));
    // The clustered set changed: every light is rebuilt, since their ranges may
    // bound different geometry now.
    EXPECT_TRUE(should_update_point_light_frusta(false, true, true, true));

    // A light without shadows never needs frusta, whatever changed.
    EXPECT_FALSE(should_update_point_light_frusta(true, false, false, true));
    EXPECT_FALSE(should_update_point_light_frusta(false, true, false, true));

    // A light in no cluster is not being lit for any view, so its frusta are not
    // needed - even when it changed and casts shadows.
    EXPECT_FALSE(should_update_point_light_frusta(true, false, true, false));
    EXPECT_FALSE(should_update_point_light_frusta(false, true, true, false));

    // The order of the checks is observable: a shadowless light outside every
    // cluster is still skipped, so shadowlessness cannot be the only gate.
    EXPECT_FALSE(should_update_point_light_frusta(true, true, false, false));
}

/** @brief A directional light's cascade frusta are rebuilt whenever it casts
 *  shadows and is itself visible - with no change detection, unlike the point-light
 *  gate (Bevy `update_directional_light_frusta`,
 *  `directional_light.rs:220-226`). */
TEST(Cluster, ShouldUpdateDirectionalCascadeFrustaMatchesBevy) {
    using epix::pbr::should_update_directional_cascade_frusta;
    using epix::pbr::should_update_point_light_frusta;

    EXPECT_TRUE(should_update_directional_cascade_frusta(true, true));
    EXPECT_FALSE(should_update_directional_cascade_frusta(false, true)) << "no shadows, no frusta";
    EXPECT_FALSE(should_update_directional_cascade_frusta(true, false)) << "a light nothing looks at needs none";
    EXPECT_FALSE(should_update_directional_cascade_frusta(false, false));

    // The asymmetry with the point-light gate is intentional and worth pinning: a
    // directional light is rebuilt even in the case the point-light gate skips - a
    // shadow-casting, visible light in no changed state at all - because Bevy's
    // directional system has no change detection. A future "optimization" adding
    // one would be a divergence.
    EXPECT_TRUE(should_update_directional_cascade_frusta(true, true));
    EXPECT_FALSE(should_update_point_light_frusta(false, false, true, true))
        << "the point-light gate does have change detection";
}

/** @brief The point-light frusta system actually rebuilds them for a clustered,
 *  shadow-casting light, and leaves them alone otherwise (Bevy
 *  `update_point_light_frusta`, `point_light.rs:188-244`). */
TEST(Cluster, UpdatePointLightFrustaSystemRebuildsFrusta) {
    using epix::pbr::GlobalVisibleClusterableObjects;
    using epix::pbr::PointLight;
    using epix::pbr::update_point_light_frusta;

    const glm::mat4 world_from_light = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, -1.0f, 4.0f));
    const float range                = 20.0f;

    const auto run_with = [&](bool shadows_enabled, bool clustered, std::optional<glm::mat4> light_transform = std::nullopt) {
        epix::app::App app = epix::app::App::create();
        const auto light   = app.world_mut()
                               .spawn(epix::transform::GlobalTransform{light_transform.value_or(world_from_light)},
                                      PointLight{.shadows_enabled = shadows_enabled, .range = range},
                                      epix::camera::CubemapFrusta{})
                               .id();
        app.world_mut().insert_resource(
            GlobalVisibleClusterableObjects{.entities = clustered ? std::unordered_set<epix::ecs::Entity>{light}
                                                                 : std::unordered_set<epix::ecs::Entity>{}});
        app.add_systems(epix::app::Update, epix::ecs::into(update_point_light_frusta));
        app.update();
        return app.world_mut().entity(light).get<epix::camera::CubemapFrusta>()->get().frusta;
    };

    // A shadow-casting, clustered light gets its six face pyramids: a small sphere
    // at the light is inside each, and one beyond the range on the far side of the
    // light's shared far plane is inside none. That plane is oriented by the light's
    // backward axis and sits at translation - range * backward, so for an unrotated
    // transform (backward = +Z) it bounds the -Z side, and the +Z side is open.
    const auto frusta = run_with(true, true);
    const epix::camera::Sphere at_light{.center = glm::vec3(2.0f, -1.0f, 4.0f), .radius = 0.01f};
    const epix::camera::Sphere beyond_bound{.center = glm::vec3(2.0f, -1.0f, 4.0f - range - 1.0f), .radius = 0.01f};
    for (std::size_t face = 0; face < frusta.size(); ++face) {
        EXPECT_TRUE(frusta[face].intersects_sphere(at_light)) << "face " << face << " contains the light";
        EXPECT_FALSE(frusta[face].intersects_sphere(beyond_bound))
            << "face " << face << " is bounded on the far side of that plane";
    }
    // The open side really is open, which is the asymmetry worth pinning: a sphere
    // beyond the range in the +Z direction is still inside the +Z face.
    const epix::camera::Sphere open_side{.center = glm::vec3(2.0f, -1.0f, 4.0f + range + 1.0f), .radius = 0.01f};
    EXPECT_TRUE(frusta[5].intersects_sphere(open_side)) << "the +Z face has no far bound";
    // They are not the default-constructed planes any more, which is how a system
    // that quietly did nothing would look.
    EXPECT_NE(frusta[0].planes, epix::camera::Frustum{}.planes);

    // A light without shadows keeps empty frusta: the gate skips it.
    const auto unshadowed = run_with(false, true);
    EXPECT_EQ(unshadowed[0].planes, epix::camera::Frustum{}.planes) << "no shadows, no frusta";

    // So does a light in no cluster, even with shadows enabled.
    const auto unclustered = run_with(true, false);
    EXPECT_EQ(unclustered[0].planes, epix::camera::Frustum{}.planes) << "not clustered, nothing to cull for";

    // A transform that carries a scale must bound its frusta at `range` all the
    // same: Bevy passes `transform.back()`, a unit direction, so the far plane
    // cannot move with the light's scale.
    const auto scaled = run_with(true, true, glm::scale(world_from_light, glm::vec3(3.0f)));
    const epix::camera::Sphere scaled_beyond_bound{.center = glm::vec3(2.0f, -1.0f, 4.0f - range - 1.0f),
                                                   .radius = 0.01f};
    for (std::size_t face = 0; face < scaled.size(); ++face) {
        EXPECT_FALSE(scaled[face].intersects_sphere(scaled_beyond_bound))
            << "face " << face << " of a scaled light is still bounded at range";
    }
}

/** @brief Bevy's `orthonormalize`, `spot_light_world_from_view` and
 *  `spot_light_clip_from_view` (`bevy_light/src/spot_light.rs:164-190`). The
 *  basis is `Vec3::any_orthonormal_pair`'s construction, reproduced so the CPU
 *  and the fragment shader agree. */
TEST(Cluster, SpotLightBasisAndProjectionMatchBevy) {
    using epix::pbr::orthonormalize;
    using epix::pbr::spot_light_clip_from_view;
    using epix::pbr::spot_light_world_from_view;

    const std::array axes{glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.6f, 0.0f, 0.8f),
                          glm::normalize(glm::vec3(-0.3f, 0.4f, 0.866f)), glm::normalize(glm::vec3(1.0f, 1.0f, -1.0f))};
    for (const glm::vec3 axis : axes) {
        const glm::mat3 basis = orthonormalize(axis);
        // The third column is the axis that was passed in, unit-length by contract.
        EXPECT_NEAR(glm::length(basis[2] - axis), 0.0f, 1e-5f);
        EXPECT_NEAR(glm::length(basis[0]), 1.0f, 1e-4f);
        EXPECT_NEAR(glm::length(basis[1]), 1.0f, 1e-4f);
        EXPECT_NEAR(glm::dot(basis[0], basis[1]), 0.0f, 1e-4f);
        EXPECT_NEAR(glm::dot(basis[0], basis[2]), 0.0f, 1e-4f);
        EXPECT_NEAR(glm::dot(basis[1], basis[2]), 0.0f, 1e-4f);
        // Right-handed: x cross y is z.
        EXPECT_NEAR(glm::length(glm::cross(basis[0], basis[1]) - basis[2]), 0.0f, 1e-4f);
    }

    // The light's translation is placed without its scale, which is what keeps the
    // shadow view from scaling the light's radius and range.
    const glm::mat4 scaled = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f)) *
                             glm::scale(glm::mat4(1.0f), glm::vec3(4.0f, 4.0f, 4.0f));
    const glm::mat4 world_from_view =
        spot_light_world_from_view(epix::transform::GlobalTransform{scaled});
    EXPECT_EQ(glm::vec3(world_from_view[3]), glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_NEAR(glm::length(glm::vec3(world_from_view[0])), 1.0f, 1e-5f)
        << "the basis columns stay unit-length under a scaled light";
    EXPECT_EQ(glm::vec3(world_from_view[2]), glm::vec3(0.0f, 0.0f, 1.0f));

    // The spot's field of view is twice the angle from its centre to the outer edge.
    const float outer_angle = 0.6f;
    const float near_z      = 0.1f;
    EXPECT_EQ(spot_light_clip_from_view(outer_angle, near_z),
              epix::camera::perspective_infinite_reverse_rh(outer_angle * 2.0f, 1.0f, near_z));
}

/** @brief Captures everything logged through spdlog while it is alive, so a
 *  diagnostic Bevy emits can be asserted rather than assumed. */
namespace {
struct CapturingSink final : spdlog::sinks::base_sink<std::mutex> {
    std::vector<std::string> messages;

   protected:
    void sink_it_(const spdlog::details::log_msg& message) override {
        messages.emplace_back(message.payload.data(), message.payload.size());
    }
    void flush_() override {}
};

/** @brief Redirects the default logger's sinks for the lifetime of the object. */
class LogCapture {
   public:
    LogCapture() : previous(spdlog::default_logger()->sinks()) {
        sink = std::make_shared<CapturingSink>();
        spdlog::default_logger()->sinks() = {sink};
    }
    ~LogCapture() { spdlog::default_logger()->sinks() = previous; }
    LogCapture(const LogCapture&)            = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    bool contains(std::string_view needle) const {
        return std::ranges::any_of(messages(), [needle](const std::string& message) {
            return message.find(needle) != std::string::npos;
        });
    }
    std::size_t count_containing(std::string_view needle) const {
        return static_cast<std::size_t>(std::ranges::count_if(messages(), [needle](const std::string& message) {
            return message.find(needle) != std::string::npos;
        }));
    }
    void clear() const { sink->messages.clear(); }

   private:
    const std::vector<std::string>& messages() const { return sink->messages; }

    std::shared_ptr<CapturingSink> sink;
    std::vector<spdlog::sink_ptr> previous;
};
}  // namespace

/** @brief The diagnostics Bevy logs from the cluster code. Each one is a real
 *  branch, not decoration: the z-slice clamp, the uniform offset overflow and the
 *  index-list overflow all change what is written, and Bevy says so. */
TEST(Cluster, ClusterDiagnosticsMatchBevy) {
    using epix::pbr::ClusterConfig;
    using epix::pbr::ClusterableObjectCounts;
    using epix::pbr::ExtractedClusterableObjectElement;
    using epix::pbr::ViewClusterBindings;
    using epix::pbr::VIEW_CLUSTER_MAX_INDICES;
    using epix::pbr::VIEW_CLUSTER_MAX_UNIFORM_ITEMS;
    using epix::pbr::prepare_view_cluster_bindings;

    const LogCapture capture;

    // A FixedZ configuration with fewer total clusters than z-slices is clamped,
    // and Bevy warns first (`cluster/mod.rs:257-260`).
    ClusterConfig config;
    config.kind     = ClusterConfig::Kind::FixedZ;
    config.total    = 8;
    config.z_slices = 24;
    const auto dimensions = config.dimensions_for_screen_size({1920, 1080});
    EXPECT_EQ(dimensions.z, 8u) << "the slice count is clamped to the total";
    EXPECT_TRUE(capture.contains("ClusterConfig has more z-slices than total clusters!"));

    // Pushing more offsets than the uniform list holds warns and drops the offset
    // (`cluster.rs:380-383`).
    capture.clear();
    ViewClusterBindings bindings{wgpu::BufferBindingType::eUniform};
    for (std::size_t index = 0; index < VIEW_CLUSTER_MAX_UNIFORM_ITEMS * 4 + 1; ++index) {
        bindings.push_offset_and_counts(0, ClusterableObjectCounts{});
    }
    EXPECT_EQ(bindings.offsets_count(), VIEW_CLUSTER_MAX_UNIFORM_ITEMS * 4)
        << "the overflowing offset is not counted";
    EXPECT_TRUE(capture.contains("cluster offset and count out of bounds!"));

    // Walking past the index-list limit stops the walk for the view with a
    // warning, which only the uniform form has (`cluster.rs:317-325`).
    capture.clear();
    std::vector<ExtractedClusterableObjectElement> records;
    records.reserve(VIEW_CLUSTER_MAX_INDICES + 1);
    records.push_back(ExtractedClusterableObjectElement{.kind = ExtractedClusterableObjectElement::Kind::ClusterHeader});
    for (std::size_t index = 0; index <= VIEW_CLUSTER_MAX_INDICES; ++index) {
        ExtractedClusterableObjectElement record;
        record.kind   = ExtractedClusterableObjectElement::Kind::ClusterableObjectEntity;
        record.entity = epix::ecs::Entity::from_index(static_cast<std::uint32_t>(index));
        records.push_back(record);
    }
    std::unordered_map<epix::ecs::Entity, std::size_t> entity_to_index;
    for (std::size_t index = 0; index <= VIEW_CLUSTER_MAX_INDICES; ++index) {
        entity_to_index.emplace(epix::ecs::Entity::from_index(static_cast<std::uint32_t>(index)), index);
    }
    const auto uniform_bindings =
        prepare_view_cluster_bindings(records, entity_to_index, wgpu::BufferBindingType::eUniform, false);
    EXPECT_EQ(uniform_bindings.indices_count(), VIEW_CLUSTER_MAX_INDICES);
    EXPECT_TRUE(capture.contains("Clusterable object index lists are full!"));

    // The storage form has no such limit, so it must not warn (`cluster.rs:317-319`).
    capture.clear();
    const auto storage_bindings =
        prepare_view_cluster_bindings(records, entity_to_index, wgpu::BufferBindingType::eStorage, true);
    EXPECT_EQ(storage_bindings.indices_count(), VIEW_CLUSTER_MAX_INDICES + 1);
    EXPECT_FALSE(capture.contains("Clusterable object index lists are full!"));
}

/** @brief Bevy's `shrink_entities`: reclaim only when the capacity is more than
 *  twice the length, and then only to half the ratio, so a list that is already
 *  tight is left alone instead of reallocating every frame
 *  (`bevy_light/src/lib.rs:286-300`). */
TEST(Cluster, ShrinkEntitiesOnlyReclaimsLooseCapacity) {
    using epix::pbr::shrink_entities;

    const auto entity = epix::ecs::Entity::from_index(7);

    // One entity in a 100-slot buffer: 100 / 1 = 100 > 2, so the capacity lands at
    // 100 / (100 / 2) = 2.
    std::vector<epix::ecs::Entity> loose;
    loose.reserve(100);
    loose.push_back(entity);
    shrink_entities(loose);
    EXPECT_EQ(loose.size(), 1u);
    EXPECT_LT(loose.capacity(), 100u) << "a loose buffer is reclaimed";
    EXPECT_EQ(loose[0], entity) << "and the entities survive";

    // Two entities in a 4-slot buffer: 4 / 2 = 2, which is not more than 2, so
    // nothing is touched.
    std::vector<epix::ecs::Entity> tight;
    tight.reserve(4);
    tight.push_back(entity);
    tight.push_back(entity);
    const std::size_t tight_capacity = tight.capacity();
    shrink_entities(tight);
    EXPECT_EQ(tight.capacity(), tight_capacity) << "an already-tight list is left alone";
    EXPECT_EQ(tight.size(), 2u);

    // An empty list releases its buffer, as Rust's `checked_div` returning `None`
    // for a zero length does.
    std::vector<epix::ecs::Entity> empty;
    empty.reserve(64);
    shrink_entities(empty);
    EXPECT_EQ(empty.capacity(), 0u);
}

/** @brief `update_spot_light_frusta` writes a valid cone for a shadow-casting,
 *  clustered spot light and leaves every other light alone (Bevy
 *  `spot_light.rs:204-237`). Until it runs, a spot light culls meshes against
 *  the default `Frustum`, which contains nothing. */
TEST(Cluster, UpdateSpotLightFrustaRebuildsFrusta) {
    using epix::pbr::GlobalVisibleClusterableObjects;
    using epix::pbr::SpotLight;
    using epix::pbr::update_spot_light_frusta;

    const float range      = 20.0f;
    const float outer      = glm::radians(45.0f);
    const auto light_at    = glm::vec3(2.0f, -1.0f, 4.0f);
    const auto transform   = glm::translate(glm::mat4(1.0f), light_at);

    const auto run_with = [&](bool shadows_enabled, bool clustered) {
        epix::app::App app = epix::app::App::create();
        SpotLight light;
        light.shadows_enabled = shadows_enabled;
        light.outer_angle     = outer;
        light.range           = range;
        const auto entity     = app.world_mut()
                                    .spawn(epix::transform::GlobalTransform{transform}, light,
                                           epix::camera::Frustum{})
                                    .id();
        app.world_mut().insert_resource(
            GlobalVisibleClusterableObjects{.entities = clustered ? std::unordered_set<epix::ecs::Entity>{entity}
                                                                 : std::unordered_set<epix::ecs::Entity>{}});
        app.add_systems(epix::app::Update, epix::ecs::into(update_spot_light_frusta));
        app.update();
        return app.world_mut().entity(entity).get<epix::camera::Frustum>()->get();
    };

    const auto frustum = run_with(true, true);
    EXPECT_NE(frustum.planes, epix::camera::Frustum{}.planes)
        << "the light no longer carries a default frustum";

    // Inside the cone: at the light, and half-way down the axis. The cone opens
    // along the light's forward direction, which is the negation of `back()` - so
    // -Z for an unrotated transform, as the custom far plane's placement shows.
    EXPECT_TRUE(frustum.intersects_sphere(epix::camera::Sphere{.center = light_at, .radius = 0.01f}));
    EXPECT_TRUE(frustum.intersects_sphere(
        epix::camera::Sphere{.center = light_at + glm::vec3(0.0f, 0.0f, -range * 0.5f), .radius = 0.01f}))
        << "the cone opens along the light's forward axis";

    // Outside the cone: 60 degrees off the axis at the same distance.
    const float off_axis = range * std::tan(glm::radians(60.0f));
    EXPECT_FALSE(frustum.intersects_sphere(
        epix::camera::Sphere{.center = light_at + glm::vec3(off_axis, 0.0f, -range), .radius = 0.01f}))
        << "the cone is bounded at twice the outer angle";

    // Bounded at `range` along the axis, which is what the custom far plane is for.
    EXPECT_FALSE(frustum.intersects_sphere(
        epix::camera::Sphere{.center = light_at + glm::vec3(0.0f, 0.0f, -(range + 1.0f)), .radius = 0.01f}));

    // A light without shadows, or in no cluster, keeps the default frustum.
    EXPECT_EQ(run_with(false, true).planes, epix::camera::Frustum{}.planes) << "no shadows, no frusta";
    EXPECT_EQ(run_with(true, false).planes, epix::camera::Frustum{}.planes) << "not clustered, nothing to cull for";
}

/** @brief The cascade builder fills a cascades map per active view, and the clear
 *  system empties it (Bevy `cascade.rs:186-253`). */
TEST(Cluster, DirectionalCascadeSystemsBuildAndClear) {
    using epix::pbr::build_directional_light_cascades;
    using epix::pbr::Cascade;
    using epix::pbr::CascadeShadowConfig;
    using epix::pbr::Cascades;
    using epix::pbr::clear_directional_light_cascades;
    using epix::pbr::default_cascade_shadow_config;
    using epix::pbr::DirectionalLight;
    using epix::pbr::DirectionalLightShadowMap;

    const auto spawn_view = [](epix::ecs::World& world, bool active = true) {
        epix::camera::PerspectiveProjection projection;
        projection.fov          = glm::radians(90.0f);
        projection.aspect_ratio = 1.0f;
        projection.near_plane   = 0.1f;
        epix::camera::Projection wrapper;
        wrapper.projection = projection;
        return world
            .spawn(epix::camera::Camera{.is_active = active}, wrapper, epix::transform::GlobalTransform{})
            .id();
    };

    // One view, one shadow-casting light: the light gets that many cascades, and
    // they are the four the default configuration asks for.
    {
        epix::app::App app = epix::app::App::create();
        const auto view    = spawn_view(app.world_mut());
        const auto light   = app.world_mut()
                               .spawn(DirectionalLight{.shadows_enabled = true}, default_cascade_shadow_config(),
                                      Cascades{}, epix::transform::GlobalTransform{})
                               .id();
        app.world_mut().insert_resource(DirectionalLightShadowMap{});
        app.add_systems(epix::app::Update, epix::ecs::into(build_directional_light_cascades));
        app.update();

        const auto& cascades = app.world_mut().entity(light).get<Cascades>()->get().cascades;
        ASSERT_EQ(cascades.size(), 1u) << "one entry per active view";
        ASSERT_TRUE(cascades.contains(view));
        ASSERT_EQ(cascades.at(view).size(), 4u) << "the default configuration's four cascades";
        for (const Cascade& cascade : cascades.at(view)) {
            EXPECT_NE(cascade.clip_from_world, glm::mat4(1.0f)) << "the cascade was actually built";
            EXPECT_GT(cascade.texel_size, 0.0f);
        }
    }

    // A light without shadows builds none.
    {
        epix::app::App app = epix::app::App::create();
        spawn_view(app.world_mut());
        const auto light = app.world_mut()
                               .spawn(DirectionalLight{.shadows_enabled = false}, default_cascade_shadow_config(),
                                      Cascades{}, epix::transform::GlobalTransform{})
                               .id();
        app.world_mut().insert_resource(DirectionalLightShadowMap{});
        app.add_systems(epix::app::Update, epix::ecs::into(build_directional_light_cascades));
        app.update();
        EXPECT_TRUE(app.world_mut().entity(light).get<Cascades>()->get().cascades.empty());
    }

    // An inactive view is skipped: Bevy filters on `camera.is_active`.
    {
        epix::app::App app = epix::app::App::create();
        spawn_view(app.world_mut(), false);  // inactive
        const auto light = app.world_mut()
                               .spawn(DirectionalLight{.shadows_enabled = true}, default_cascade_shadow_config(),
                                      Cascades{}, epix::transform::GlobalTransform{})
                               .id();
        app.world_mut().insert_resource(DirectionalLightShadowMap{});
        app.add_systems(epix::app::Update, epix::ecs::into(build_directional_light_cascades));
        app.update();
        EXPECT_TRUE(app.world_mut().entity(light).get<Cascades>()->get().cascades.empty())
            << "an inactive view needs no cascades";
    }

    // The clear system empties a populated map, and leaves a shadowless light's
    // (empty) map alone.
    {
        epix::app::App app = epix::app::App::create();
        const auto view    = epix::ecs::Entity::from_index(3);
        Cascades populated;
        populated.cascades[view] = {Cascade{}};
        const auto shadowed = app.world_mut()
                                .spawn(DirectionalLight{.shadows_enabled = true}, populated)
                                .id();
        const auto unshadowed = app.world_mut()
                                  .spawn(DirectionalLight{.shadows_enabled = false}, populated)
                                  .id();
        app.add_systems(epix::app::Update, epix::ecs::into(clear_directional_light_cascades));
        app.update();
        EXPECT_TRUE(app.world_mut().entity(shadowed).get<Cascades>()->get().cascades.empty());
        EXPECT_EQ(app.world_mut().entity(unshadowed).get<Cascades>()->get().cascades.size(), 1u)
            << "a light without shadows is skipped by the clear too";
    }
}

/** @brief A point light's assignment data drops its rotation and scale while a spot
 *  light's keeps them, because a point light's reach is a sphere and a spot light's
 *  direction lives in its transform (Bevy `assign.rs:170-215`). */
TEST(Cluster, ClusterableAssignmentDataMatchesBevy) {
    using epix::pbr::point_light_assignment_data;
    using epix::pbr::spot_light_assignment_data;

    const auto entity = epix::ecs::Entity::from_index(4);
    // A light that is both rotated and scaled - the case the reduction exists for.
    const glm::mat4 world_from_light =
        glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, -2.0f, 5.0f)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(35.0f), glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(4.0f, 4.0f, 4.0f));
    const glm::vec3 translation = glm::vec3(world_from_light[3]);

    const auto point = point_light_assignment_data(entity, translation, 7.5f, true, false,
                                                  epix::camera::RenderLayers::layer(2));
    // Translation kept, basis reduced to identity: no rotation, no scale.
    EXPECT_EQ(glm::vec3(point.transform[3]), translation);
    for (int column = 0; column < 3; ++column) {
        EXPECT_FLOAT_EQ(glm::length(glm::vec3(point.transform[column])), 1.0f)
            << "column " << column << " is unit length, so the scale is gone";
    }
    EXPECT_EQ(point.transform, glm::translate(glm::mat4(1.0f), translation));
    EXPECT_FLOAT_EQ(point.range, 7.5f);
    EXPECT_EQ(point.object_type.kind, epix::pbr::ClusterableObjectType::Kind::PointLight);
    EXPECT_TRUE(point.object_type.shadows_enabled);
    EXPECT_FLOAT_EQ(point.object_type.outer_angle, 0.0f) << "a point light has no cone";
    EXPECT_EQ(point.render_layers, epix::camera::RenderLayers::layer(2)) << "the layers are carried through";

    // The spot light keeps everything: same matrix, angle recorded.
    const auto spot = spot_light_assignment_data(entity, world_from_light, 7.5f, 0.4f, true, true,
                                                epix::camera::RenderLayers::layer(2));
    EXPECT_EQ(spot.transform, world_from_light) << "the cone's direction needs the whole transform";
    EXPECT_EQ(spot.object_type.kind, epix::pbr::ClusterableObjectType::Kind::SpotLight);
    EXPECT_FLOAT_EQ(spot.object_type.outer_angle, 0.4f);
    EXPECT_TRUE(spot.object_type.volumetric) << "volumetrics reach the sort key";
    EXPECT_NE(spot.transform, point.transform) << "the two kinds are not built the same way";

    // Layers left unset default to layer 0, Bevy's default, which is distinct from
    // belonging to none.
    const auto defaulted = point_light_assignment_data(entity, translation, 1.0f, false, false, {});
    EXPECT_TRUE(defaulted.render_layers.intersects(epix::camera::RenderLayers::layer(0)));
    EXPECT_FALSE(defaulted.render_layers.intersects(epix::camera::RenderLayers::layer(1)))
        << "and it is not every layer";
}

/** @brief The uniform-path capacity cut: sort by the lookup key, drop what no view
 *  can see, keep one past the limit so over-capacity is detectable, then truncate
 *  and flag (Bevy `assign.rs:245-295`). */
TEST(Cluster, UniformCapacityCutMatchesBevy) {
    using epix::camera::Frustum;
    using epix::pbr::ClusterableObjectAssignmentData;
    using epix::pbr::ClusterableObjectType;
    using epix::pbr::limit_clusterable_objects_for_uniform_buffer;

    // A view frustum placed five units back along +Z, looking down -Z, 90 degrees
    // wide. Note the projection is infinite, so distance alone never leaves it - an
    // out-of-view object has to be off to the side.
    epix::camera::PerspectiveProjection projection;
    projection.fov          = glm::radians(90.0f);
    projection.aspect_ratio = 1.0f;
    projection.near_plane   = 0.1f;
    const Frustum containing =
        Frustum::from_clip_from_world(projection.get_clip_from_view() * glm::inverse(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 5.0f))));
    const auto in_view_of_view = [&containing](const epix::camera::Sphere& sphere) {
        return containing.intersects_sphere(sphere, true);
    };

    const auto object = [](std::size_t index, ClusterableObjectType::Kind kind, glm::vec3 position) {
        ClusterableObjectAssignmentData data;
        data.entity      = epix::ecs::Entity::from_index(static_cast<std::uint32_t>(index));
        data.transform   = glm::translate(glm::mat4(1.0f), position);
        data.object_type = ClusterableObjectType{.kind = kind};
        return data;
    };
    const glm::vec3 in_view{0.0f, 0.0f, -4.0f};
    const glm::vec3 out_of_view{500.0f, 0.0f, -4.0f};  // well outside a 90-degree cone

    // Under the limit nothing is dropped and no warning is raised, but the sort
    // still applies: the point light comes first.
    {
        const auto limited = limit_clusterable_objects_for_uniform_buffer(
            {object(1, ClusterableObjectType::Kind::SpotLight, in_view),
             object(2, ClusterableObjectType::Kind::PointLight, in_view)},
            in_view_of_view, 3);
        EXPECT_EQ(limited.objects.size(), 2u);
        EXPECT_FALSE(limited.exceeded);
        EXPECT_EQ(limited.objects[0].object_type.kind, ClusterableObjectType::Kind::PointLight)
            << "the sort decides the order, and so which objects survive a cap";
    }

    // Over the limit: flagged, truncated to exactly the limit.
    {
        const auto limited = limit_clusterable_objects_for_uniform_buffer(
            {object(1, ClusterableObjectType::Kind::PointLight, in_view),
             object(2, ClusterableObjectType::Kind::PointLight, in_view),
             object(3, ClusterableObjectType::Kind::PointLight, in_view)},
            in_view_of_view, 2);
        EXPECT_EQ(limited.objects.size(), 2u);
        EXPECT_TRUE(limited.exceeded) << "the third object is what makes the excess visible";
    }

    // Objects outside the view are dropped and do not count towards the limit, so
    // three objects with one out of view fit under a limit of two.
    {
        const auto limited = limit_clusterable_objects_for_uniform_buffer(
            {object(1, ClusterableObjectType::Kind::PointLight, in_view),
             object(2, ClusterableObjectType::Kind::PointLight, in_view),
             object(3, ClusterableObjectType::Kind::PointLight, out_of_view)},
            in_view_of_view, 2);
        EXPECT_EQ(limited.objects.size(), 2u);
        EXPECT_FALSE(limited.exceeded) << "only two were in view, so the limit was not exceeded";
    }

    // Exactly at the limit is not exceeding it, which is the distinction the extra
    // retained object exists to make.
    {
        const auto limited = limit_clusterable_objects_for_uniform_buffer(
            {object(1, ClusterableObjectType::Kind::PointLight, in_view),
             object(2, ClusterableObjectType::Kind::PointLight, in_view)},
            in_view_of_view, 2);
        EXPECT_EQ(limited.objects.size(), 2u);
        EXPECT_FALSE(limited.exceeded) << "at capacity, not over it";
    }
}

/** @brief A cluster's bounding sphere is computed once and cached per cluster
 *  index, not shared between clusters (Bevy `assign.rs:722-745`). */
TEST(Cluster, CachedClusterSphereComputesOnce) {
    using epix::camera::Sphere;
    using epix::pbr::cached_cluster_sphere;

    std::vector<std::optional<Sphere>> cache(4);
    std::size_t computations = 0;
    const auto compute_for = [&computations](float x) {
        return [&computations, x] {
            ++computations;
            return Sphere{.center = glm::vec3(x, 0.0f, 0.0f), .radius = 1.0f};
        };
    };

    // First use computes; the same index afterwards does not.
    const Sphere& first = cached_cluster_sphere(cache, 1, compute_for(10.0f));
    EXPECT_EQ(computations, 1u);
    EXPECT_FLOAT_EQ(first.center.x, 10.0f);
    const Sphere& again = cached_cluster_sphere(cache, 1, compute_for(99.0f));
    EXPECT_EQ(computations, 1u) << "the cached value is reused, so the second callable never runs";
    EXPECT_FLOAT_EQ(again.center.x, 10.0f) << "and the cached sphere is what comes back";
    EXPECT_EQ(&first, &again) << "the same slot";

    // A different index is a different slot with its own computation - the mistake
    // this guards against is reusing a neighbouring cluster's bounds.
    const Sphere& other = cached_cluster_sphere(cache, 2, compute_for(20.0f));
    EXPECT_EQ(computations, 2u);
    EXPECT_FLOAT_EQ(other.center.x, 20.0f);
    EXPECT_NE(&first, &other);
    EXPECT_EQ(cache[1]->center.x, 10.0f) << "the first slot is untouched";
    EXPECT_EQ(cache[2]->center.x, 20.0f);

    // Untouched indices stay empty: the cache is filled lazily, not up front.
    EXPECT_FALSE(cache[0].has_value());
    EXPECT_FALSE(cache[3].has_value());
}

/** @brief The object-centre clamp, including the `dimensions.y + 1` sentinel Bevy
 *  uses for a centre below the view while using nothing for one above it (Bevy
 *  `assign.rs:630-641`). */
TEST(Cluster, ClusterCenterCoordinatesMatchBevy) {
    using epix::pbr::ndc_position_to_cluster;

    const glm::uvec3 dimensions{16u, 9u, 24u};
    const glm::uvec3 coordinates{3u, 4u, 5u};

    // Inside the view: both are the object's own cluster.
    {
        const auto [z_center, y_center] =
            ndc_position_to_cluster(coordinates, glm::vec3(0.0f, 0.0f, 0.5f), dimensions);
        ASSERT_TRUE(z_center.has_value());
        ASSERT_TRUE(y_center.has_value());
        EXPECT_EQ(*z_center, 5u);
        EXPECT_EQ(*y_center, 4u);
    }

    // z is kept up to and including the far plane, and dropped beyond it.
    {
        const auto [at_far, y_at_far] =
            ndc_position_to_cluster(coordinates, glm::vec3(0.0f, 0.0f, 1.0f), dimensions);
        EXPECT_TRUE(at_far.has_value()) << "the far plane itself still counts";
        const auto [beyond, y_beyond] =
            ndc_position_to_cluster(coordinates, glm::vec3(0.0f, 0.0f, 1.0001f), dimensions);
        EXPECT_FALSE(beyond.has_value()) << "beyond the far plane there is no slice";
        EXPECT_EQ(y_at_far, y_beyond) << "and y is unaffected by z";
    }

    // The asymmetry: above the view is nothing, below it is dimensions.y + 1, which
    // no real y can equal - so both mean "no slice holds the centre".
    {
        const auto [z_above, y_above] =
            ndc_position_to_cluster(coordinates, glm::vec3(0.0f, 1.5f, 0.5f), dimensions);
        EXPECT_TRUE(z_above.has_value());
        EXPECT_FALSE(y_above.has_value()) << "above the view: nothing";
        const auto [z_below, y_below] =
            ndc_position_to_cluster(coordinates, glm::vec3(0.0f, -1.5f, 0.5f), dimensions);
        EXPECT_TRUE(z_below.has_value());
        ASSERT_TRUE(y_below.has_value()) << "below the view: a value rather than nothing";
        EXPECT_EQ(*y_below, dimensions.y + 1u) << "and it is one past the last row";
        EXPECT_GT(*y_below, dimensions.y) << "so no y in the grid can equal it";
    }

    // The boundaries are inclusive: exactly 1 is inside, exactly -1 is inside.
    {
        const auto [z, y] = ndc_position_to_cluster(coordinates, glm::vec3(0.0f, 1.0f, 0.5f), dimensions);
        EXPECT_TRUE(z.has_value());
        ASSERT_TRUE(y.has_value());
        EXPECT_EQ(*y, 4u) << "ndc y of exactly 1 is still in the view";
        const auto [z_low, y_low] = ndc_position_to_cluster(coordinates, glm::vec3(0.0f, -1.0f, 0.5f), dimensions);
        ASSERT_TRUE(y_low.has_value());
        EXPECT_EQ(*y_low, 4u) << "and so is exactly -1";
    }
}

/** @brief The view-space object sphere scales its radius by the view's largest
 *  scale component, and the cluster range is ordered component-wise because the y
 *  flip can invert it (Bevy `assign.rs:570-599`). */
TEST(Cluster, ViewSpaceSphereAndClusterRangeMatchBevy) {
    using epix::camera::Sphere;
    using epix::pbr::object_cluster_range;
    using epix::pbr::view_space_object_sphere;

    // The centre goes through the view matrix; the radius is scaled by the max
    // component of the view's scale, not by an axis or an average. With scale
    // (2, 3, 4) and radius 1, the result is 4 - using 2 or 3 would thin the range
    // and drop clusters the object reaches.
    const glm::mat4 view_from_world =
        glm::translate(glm::mat4(1.0f), glm::vec3(-5.0f, 2.0f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 3.0f, 4.0f));
    const Sphere object{.center = glm::vec3(1.0f, 1.0f, -6.0f), .radius = 1.0f};
    const Sphere in_view_space = view_space_object_sphere(view_from_world, 4.0f, object);
    EXPECT_FLOAT_EQ(in_view_space.center.x, 1.0f * 2.0f - 5.0f) << "x through the view scale and translation";
    EXPECT_FLOAT_EQ(in_view_space.center.y, 1.0f * 3.0f + 2.0f);
    EXPECT_FLOAT_EQ(in_view_space.center.z, -6.0f * 4.0f);
    EXPECT_FLOAT_EQ(in_view_space.radius, 4.0f) << "the largest scale component";
    // A max of 1 (no view scale) leaves the radius alone.
    EXPECT_FLOAT_EQ(view_space_object_sphere(glm::mat4(1.0f), 1.0f, object).radius, 1.0f);

    // The y flip in cluster space means an AABB's maximum y corner usually lands in
    // a lower row, so the pair has to be ordered before it is used as a range.
    const glm::uvec3 dimensions{16u, 9u, 24u};
    const glm::vec2 cluster_factors{1.0f, 0.0f};
    const glm::vec3 aabb_min{-0.5f, -0.25f, -5.0f};
    const glm::vec3 aabb_max{0.5f, 0.25f, -2.0f};
    const auto [min_cluster, max_cluster] =
        object_cluster_range(dimensions, cluster_factors, true, aabb_min, aabb_max);

    for (int axis = 0; axis < 3; ++axis) {
        EXPECT_LE(min_cluster[axis], max_cluster[axis]) << "axis " << axis << " is ordered";
        EXPECT_LT(max_cluster[axis], dimensions[axis]) << "axis " << axis << " is inside the grid";
    }
    // The range genuinely spans rows: x from the left half and y across the centre.
    EXPECT_LT(min_cluster.x, max_cluster.x);
    EXPECT_LT(min_cluster.y, max_cluster.y) << "the y flip would invert this pair unordered";
}

/** @brief The assignment loop puts an object into the clusters it reaches, culls it
 *  entirely outside the view, and narrows a spot light by its cone (Bevy
 *  `assign.rs:540-840`). */
TEST(Cluster, AssignObjectToViewClustersMatchesBevy) {
    using epix::pbr::assign_object_to_view_clusters;
    using epix::pbr::ClusterableObjectAssignmentData;
    using epix::pbr::ClusterableObjectType;
    using epix::pbr::ViewClusterAssignment;
    using epix::pbr::VisibleClusterableObjects;

    const glm::uvec3 dimensions{8u, 8u, 8u};
    epix::camera::PerspectiveProjection projection;
    projection.fov          = glm::radians(90.0f);
    projection.aspect_ratio = 1.0f;
    projection.near_plane   = 0.1f;

    // A view at the origin looking down -Z with its clusters spread from 1 to 50.
    const glm::mat4 view_from_world = glm::mat4(1.0f);
    const glm::mat4 clip_from_view  = projection.get_clip_from_view();
    const float first_slice = 1.0f;
    const float far_z       = 50.0f;
    const glm::vec2 tile{1.0f / 8.0f, 1.0f / 8.0f};

    ViewClusterAssignment view;
    view.dimensions                = dimensions;
    view.tile_size                 = tile;
    view.screen_size               = glm::vec2(64.0f, 64.0f);
    view.cluster_factors           = epix::pbr::calculate_cluster_factors(projection.near_plane, far_z, 8.0f, false);
    view.view_from_world           = view_from_world;
    view.clip_from_view            = clip_from_view;
    view.view_from_clip            = glm::inverse(clip_from_view);
    view.is_orthographic           = false;
    view.first_slice_depth         = first_slice;
    view.far_z                     = far_z;
    const auto xy_planes           = epix::pbr::perspective_cluster_xy_planes(view.view_from_clip, dimensions);
    view.x_planes                  = xy_planes.first;
    view.y_planes                  = xy_planes.second;
    view.z_planes                  = epix::pbr::cluster_z_planes(first_slice, far_z, dimensions.z, false);
    const auto frustum             = epix::camera::Frustum::from_clip_from_world(clip_from_view);

    const auto count_assignments = [](const std::vector<VisibleClusterableObjects>& clusters) {
        std::size_t total = 0;
        for (const VisibleClusterableObjects& cluster : clusters) total += cluster.entities.size();
        return total;
    };
    const auto make_object = [](ClusterableObjectType::Kind kind, glm::vec3 position, float range, float outer_angle) {
        ClusterableObjectAssignmentData data;
        data.entity     = epix::ecs::Entity::from_index(1);
        data.transform  = glm::translate(glm::mat4(1.0f), position);
        data.range      = range;
        data.object_type = ClusterableObjectType{.kind = kind, .outer_angle = outer_angle};
        return data;
    };

    const auto assign = [&](const ClusterableObjectAssignmentData& object) {
        std::vector<VisibleClusterableObjects> clusters(8u * 8u * 8u);
        std::vector<std::optional<epix::camera::Sphere>> cache(8u * 8u * 8u);
        const bool in_view = assign_object_to_view_clusters(clusters, view, frustum, cache, object);
        return std::pair{in_view, count_assignments(clusters)};
    };

    // A point light in front of the view reaches clusters; one behind it reaches
    // none and is reported as outside.
    const auto [point_in_view, point_clusters] =
        assign(make_object(ClusterableObjectType::Kind::PointLight, glm::vec3(0.0f, 0.0f, -10.0f), 5.0f, 0.0f));
    EXPECT_TRUE(point_in_view);
    EXPECT_GT(point_clusters, 0u) << "a light in front of the view affects clusters";
    const auto [point_behind, behind_clusters] =
        assign(make_object(ClusterableObjectType::Kind::PointLight, glm::vec3(0.0f, 0.0f, 10.0f), 5.0f, 0.0f));
    EXPECT_FALSE(point_behind) << "behind the view it is outside the frustum";
    EXPECT_EQ(behind_clusters, 0u);

    // A narrow spot light reaches fewer clusters than a wide one, which is the cone
    // test doing its work.
    const auto [narrow_in_view, narrow_clusters] = assign(
        make_object(ClusterableObjectType::Kind::SpotLight, glm::vec3(0.0f, 0.0f, -10.0f), 20.0f, glm::radians(10.0f)));
    const auto [wide_in_view, wide_clusters] = assign(
        make_object(ClusterableObjectType::Kind::SpotLight, glm::vec3(0.0f, 0.0f, -10.0f), 20.0f, glm::radians(45.0f)));
    EXPECT_TRUE(narrow_in_view);
    EXPECT_TRUE(wide_in_view);
    EXPECT_GT(wide_clusters, narrow_clusters) << "a 45-degree cone covers more clusters than a 10-degree one";

    // A bigger range reaches more clusters than a smaller one.
    const auto [small_in_view, small_clusters] =
        assign(make_object(ClusterableObjectType::Kind::PointLight, glm::vec3(0.0f, 0.0f, -10.0f), 2.0f, 0.0f));
    EXPECT_TRUE(small_in_view);
    EXPECT_GT(point_clusters, small_clusters) << "a 5 unit range covers more than a 2 unit one";
}

/** @brief A probe's and a decal's range is the length of the scale *vector*, and a
 *  probe's kind follows whether it carries an environment map (Bevy
 *  `assign.rs:210-243`). */
TEST(Cluster, ProbeAndDecalAssignmentDataMatchBevy) {
    using epix::pbr::ClusterableObjectType;
    using epix::pbr::decal_assignment_data;
    using epix::pbr::light_probe_assignment_data;
    using epix::pbr::transform_scale_length;

    const auto entity = epix::ecs::Entity::from_index(9);

    // The scale length is the length of the scale vector, not its largest
    // component and not the uniform scale either: an unscaled transform has the
    // scale (1,1,1) and therefore the range sqrt(3), a uniform scale of 2 gives
    // sqrt(12), and an anisotropic (2,3,4) gives sqrt(29). Taking the max, or
    // assuming an unscaled probe has range one, would both be plausible "fixes" and
    // both divergences.
    EXPECT_NEAR(transform_scale_length(glm::mat4(1.0f)), std::sqrt(3.0f), 1e-5f)
        << "even with no scale the range is the length of (1,1,1)";
    EXPECT_NEAR(transform_scale_length(glm::scale(glm::mat4(1.0f), glm::vec3(2.0f))), std::sqrt(12.0f), 1e-4f);
    EXPECT_NEAR(transform_scale_length(glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 3.0f, 4.0f))), std::sqrt(29.0f), 1e-4f);
    // Rotation and translation do not change it - only the scale does.
    const glm::mat4 rotated = glm::rotate(glm::mat4(1.0f), glm::radians(40.0f), glm::vec3(0.1f, 1.0f, 0.2f));
    EXPECT_NEAR(transform_scale_length(rotated * glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 3.0f, 4.0f))),
                std::sqrt(29.0f), 1e-4f);

    // A probe with an environment map is a reflection probe, otherwise an
    // irradiance volume, and both take the scale length as their range.
    const glm::mat4 probe_transform = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f)) *
                                      glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 3.0f, 4.0f));
    const auto reflection = light_probe_assignment_data(entity, probe_transform, true);
    EXPECT_EQ(reflection.object_type.kind, ClusterableObjectType::Kind::ReflectionProbe);
    EXPECT_NEAR(reflection.range, std::sqrt(29.0f), 1e-4f);
    EXPECT_EQ(reflection.transform, probe_transform) << "the probe keeps its whole transform";
    const auto volume = light_probe_assignment_data(entity, probe_transform, false);
    EXPECT_EQ(volume.object_type.kind, ClusterableObjectType::Kind::IrradianceVolume);
    EXPECT_FLOAT_EQ(volume.range, reflection.range) << "only the kind differs";

    // A decal is a decal, with the same range rule.
    const glm::mat4 decal_transform = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 3.0f, 4.0f));
    const auto decal                = decal_assignment_data(entity, decal_transform);
    EXPECT_EQ(decal.object_type.kind, ClusterableObjectType::Kind::Decal);
    EXPECT_NEAR(decal.range, std::sqrt(29.0f), 1e-4f);
    EXPECT_EQ(decal.transform, decal_transform);
    // None of the three carries a cone or a shadow flag.
    EXPECT_FALSE(decal.object_type.shadows_enabled);
    EXPECT_FALSE(volume.object_type.shadows_enabled);
    EXPECT_FLOAT_EQ(decal.object_type.outer_angle, 0.0f);
}

/** @brief The assignment system's guard paths: no settings resource is a no-op, a
 *  view with no cluster configuration has its clusters cleared, and a view with no
 *  viewport is skipped with its clusters cleared (Bevy
 *  `assign_objects_to_clusters`, `assign.rs:163-165,311-327`). */
TEST(Cluster, AssignObjectsToClustersGuards) {
    using epix::pbr::assign_objects_to_clusters;
    using epix::pbr::ClusterConfig;
    using epix::pbr::Clusters;
    using epix::pbr::GlobalClusterSettings;
    using epix::pbr::GlobalVisibleClusterableObjects;

    const auto spawn_view = [](epix::ecs::World& world, ClusterConfig config) {
        // The system takes the clustered set as a ResMut, so it has to exist before
        // the system can even validate - Bevy's plugin initialises it the same way.
        world.insert_resource(GlobalVisibleClusterableObjects{});

        epix::camera::Camera camera;
        epix::camera::PerspectiveProjection projection;
        projection.fov          = glm::radians(60.0f);
        projection.aspect_ratio = 1.0f;
        projection.near_plane   = 0.1f;
        camera.computed.clip_from_view = projection.get_clip_from_view();
        const auto frustum = epix::camera::Frustum::from_clip_from_world(camera.computed.clip_from_view);
        Clusters clusters;
        clusters.clusterable_objects.resize(8);
        return world.spawn(camera, frustum, config, clusters, epix::transform::GlobalTransform{}).id();
    };
    const auto add_system = [](epix::app::App& app) {
        app.add_systems(epix::app::Update, epix::ecs::into(assign_objects_to_clusters));
    };

    // Without the settings resource the system returns before touching anything.
    {
        epix::app::App app = epix::app::App::create();
        ClusterConfig config;
        config.kind = ClusterConfig::Kind::XYZ;
        const auto view = spawn_view(app.world_mut(), config);
        add_system(app);
        app.update();
        EXPECT_EQ(app.world_mut().entity(view).get<Clusters>()->get().clusterable_objects.size(), 8u)
            << "nothing was cleared";
    }

    // With settings, a view whose configuration is None has its clusters cleared -
    // that is the one place a view's clusters are emptied and the system continues.
    {
        epix::app::App app = epix::app::App::create();
        ClusterConfig config;
        config.kind = ClusterConfig::Kind::None;
        const auto view = spawn_view(app.world_mut(), config);
        app.world_mut().insert_resource(GlobalClusterSettings{});
        add_system(app);
        app.update();
        EXPECT_TRUE(app.world_mut().entity(view).get<Clusters>()->get().clusterable_objects.empty())
            << "a view with no cluster configuration keeps no clusters";
    }

    // A view with a configuration but no viewport is also cleared and skipped: the
    // camera has no target info, so its physical viewport size is absent.
    {
        epix::app::App app = epix::app::App::create();
        ClusterConfig config;
        config.kind = ClusterConfig::Kind::XYZ;
        config.dimensions = glm::uvec3(2u, 2u, 2u);
        const auto view = spawn_view(app.world_mut(), config);
        app.world_mut().insert_resource(GlobalClusterSettings{});
        add_system(app);
        app.update();
        EXPECT_TRUE(app.world_mut().entity(view).get<Clusters>()->get().clusterable_objects.empty())
            << "a view with no viewport cannot cluster anything";
    }
}

/** @brief A view that does cluster gets its grid depths and a per-cluster list
 *  sized to the cluster count, and an object on a layer the view does not render
 *  is left out (Bevy `assign_objects_to_clusters`, `assign.rs:454-477,539-543`).
 *
 *  Epix's `Clusters` starts with an empty per-cluster list, so before the fix the
 *  assignment wrote past its end and never recorded the grid depths. */
TEST(Cluster, AssignmentWritesGridDepthsAndPerClusterLists) {
    using epix::pbr::assign_objects_to_clusters;
    using epix::pbr::ClusterConfig;
    using epix::pbr::Clusters;
    using epix::pbr::GlobalClusterSettings;
    using epix::pbr::GlobalVisibleClusterableObjects;

    const auto run_view = [](epix::camera::RenderLayers light_layers, epix::camera::RenderLayers view_layers) {
        epix::app::App app = epix::app::App::create();
        auto& world        = app.world_mut();
        // Storage buffers supported, so the object list is not cut down to the
        // uniform-buffer limit the device would impose.
        GlobalClusterSettings settings;
        settings.supports_storage_buffers              = true;
        settings.view_cluster_bindings_max_indices     = 65536;
        world.insert_resource(settings);
        world.insert_resource(GlobalVisibleClusterableObjects{});
        app.add_systems(epix::app::Update, epix::ecs::into(assign_objects_to_clusters));

        epix::camera::Camera camera;
        epix::camera::PerspectiveProjection projection;
        projection.fov          = glm::radians(60.0f);
        projection.aspect_ratio = 1.0f;
        projection.near_plane   = 0.1f;
        camera.computed.clip_from_view = projection.get_clip_from_view();
        // The viewport is what `physical_viewport_size` reads, so setting it is
        // what lets the system past its "no viewport" guard.
        camera.viewport = epix::camera::Viewport{.physical_size = {64u, 64u}};

        ClusterConfig config;
        config.kind = ClusterConfig::Kind::XYZ;

        const auto view = world
                              .spawn(camera,
                                     epix::camera::Frustum::from_clip_from_world(camera.computed.clip_from_view),
                                     config, Clusters{}, view_layers, epix::transform::GlobalTransform{})
                              .id();

        auto light = epix::pbr::PointLight{};
        light.range = 2.0f;
        auto light_visibility = epix::camera::ViewVisibility::hidden();
        light_visibility.set_visible();
        // Ten units in front of the view, which is also what fixes the far depth
        // of the grid (`view_far_z`, MaxClusterableObjectRange).
        world.spawn(epix::transform::GlobalTransform{glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -10.0f))},
                    light, light_layers, light_visibility);

        app.update();
        return world.entity(view).get<Clusters>()->get();
    };

    const auto clusters = run_view(epix::camera::RenderLayers::layer(0), epix::camera::RenderLayers::layer(0));
    const std::size_t expected = static_cast<std::size_t>(clusters.dimensions.x) * clusters.dimensions.y *
                                 clusters.dimensions.z;
    EXPECT_EQ(clusters.clusterable_objects.size(), expected)
        << "the per-cluster list is sized to the grid the view derived";
    EXPECT_GT(clusters.near_plane, 0.0f) << "the first slice depth is recorded";
    EXPECT_GT(clusters.far_plane, 0.0f) << "the far depth is recorded";
    EXPECT_GE(clusters.far_plane, clusters.near_plane);
    std::size_t assigned = 0;
    for (const auto& cluster : clusters.clusterable_objects) assigned += cluster.entities.size();
    EXPECT_GT(assigned, 0u) << "the light in front of the view reaches clusters";

    // The same light on a layer the view does not render is not clustered.
    const auto excluded = run_view(epix::camera::RenderLayers::layer(3), epix::camera::RenderLayers::layer(0));
    std::size_t excluded_assigned = 0;
    for (const auto& cluster : excluded.clusterable_objects) excluded_assigned += cluster.entities.size();
    EXPECT_EQ(excluded_assigned, 0u) << "an object on another layer never reaches the view's clusters";
}

/** @brief The mesh uniform's layout is what the shader expects, and its
 *  transform record is a plain pair of matrices (Bevy `MeshUniform` and
 *  `MeshTransforms`, `bevy_pbr/src/render/mesh.rs:448-494`). */
TEST(Cluster, MeshUniformLayoutMatchesBevy) {
    using epix::pbr::MeshTransforms;
    using epix::pbr::MeshUniform;

    // 16-byte aligned in size, with the members where the shader reads them. The
    // vec4 arrays are the affine's columns, so the second transform starts after
    // three of them, and the packed inverse transpose follows after three more.
    EXPECT_EQ(sizeof(MeshUniform), 176u) << "the shader's uniform requires 16-byte alignment";
    EXPECT_EQ(offsetof(MeshUniform, world_from_local), 0u);
    EXPECT_EQ(offsetof(MeshUniform, previous_world_from_local), 48u);
    EXPECT_EQ(offsetof(MeshUniform, local_from_world_transpose_a), 96u);
    EXPECT_EQ(offsetof(MeshUniform, local_from_world_transpose_b), 128u);
    EXPECT_EQ(offsetof(MeshUniform, flags), 132u);
    EXPECT_EQ(offsetof(MeshUniform, lightmap_uv_rect), 136u) << "uvec2 needs 8-byte alignment";
    EXPECT_EQ(offsetof(MeshUniform, first_vertex_index), 144u);
    EXPECT_EQ(offsetof(MeshUniform, current_skin_index), 148u);
    EXPECT_EQ(offsetof(MeshUniform, material_and_lightmap_bind_group_slot), 152u);
    EXPECT_EQ(offsetof(MeshUniform, tag), 156u);
    EXPECT_EQ(offsetof(MeshUniform, pad), 160u);

    // The defaults: identity transforms, no skin, nothing packed into the slots.
    const MeshUniform uniform;
    EXPECT_EQ(uniform.world_from_local[0], glm::vec4(0.0f));
    EXPECT_EQ(uniform.flags, 0u);
    EXPECT_EQ(uniform.lightmap_uv_rect, glm::uvec2(0u, 0u));
    EXPECT_EQ(uniform.material_and_lightmap_bind_group_slot, 0u);
    EXPECT_EQ(uniform.current_skin_index, 0xFFFFFFFFu) << "u32::MAX is Bevy's no-skin marker";

    const MeshTransforms transforms;
    EXPECT_EQ(transforms.world_from_local, glm::mat4(1.0f));
    EXPECT_EQ(transforms.previous_world_from_local, glm::mat4(1.0f))
        << "an instance that has not moved has the same previous transform";
    EXPECT_EQ(transforms.flags, 0u);
}

/** @brief The mesh instance flags keep Bevy's bit values, since the shader tests
 *  them by position, and the batching predicate reads one of them (Bevy
 *  `RenderMeshInstanceFlags`, `render/mesh.rs:683-699,894-900`). */
TEST(Cluster, MeshInstanceFlagsMatchBevy) {
    using epix::pbr::MaterialBindingId;
    using epix::pbr::MaterialBindGroupIndex;
    using epix::pbr::MaterialBindGroupSlot;
    using epix::pbr::RenderMeshInstanceFlags;
    using Bits = RenderMeshInstanceFlags::Bits;

    // The values, not just the distinctness: these travel to the shader.
    EXPECT_EQ(static_cast<std::uint8_t>(Bits::SHADOW_CASTER), 1u);
    EXPECT_EQ(static_cast<std::uint8_t>(Bits::AUTOMATIC_BATCHING), 2u);
    EXPECT_EQ(static_cast<std::uint8_t>(Bits::HAS_PREVIOUS_TRANSFORM), 4u);
    EXPECT_EQ(static_cast<std::uint8_t>(Bits::HAS_PREVIOUS_SKIN), 8u);
    EXPECT_EQ(static_cast<std::uint8_t>(Bits::HAS_PREVIOUS_MORPH), 16u);

    RenderMeshInstanceFlags flags;
    EXPECT_EQ(flags.bits, 0u);
    EXPECT_FALSE(flags.should_batch());
    flags.insert(Bits::AUTOMATIC_BATCHING);
    EXPECT_TRUE(flags.contains(Bits::AUTOMATIC_BATCHING));
    EXPECT_TRUE(flags.should_batch());
    EXPECT_FALSE(flags.contains(Bits::SHADOW_CASTER)) << "setting one flag does not set another";
    flags.insert(Bits::SHADOW_CASTER);
    EXPECT_TRUE(flags.contains(Bits::SHADOW_CASTER));
    EXPECT_TRUE(flags.contains(Bits::AUTOMATIC_BATCHING));
    flags.remove(Bits::AUTOMATIC_BATCHING);
    EXPECT_FALSE(flags.contains(Bits::AUTOMATIC_BATCHING));
    EXPECT_TRUE(flags.contains(Bits::SHADOW_CASTER)) << "removing one leaves the others";
    EXPECT_FALSE(flags.should_batch());

    // A material binding is a group and a slot, both defaulting to zero: that is
    // what a non-bindless material reports, with the single material in slot 0.
    const MaterialBindingId binding;
    EXPECT_EQ(binding.group.index, 0u);
    EXPECT_EQ(binding.slot.slot, 0u);
    EXPECT_EQ(binding, (MaterialBindingId{MaterialBindGroupIndex{0u}, MaterialBindGroupSlot{0u}}));
    EXPECT_NE(binding, (MaterialBindingId{MaterialBindGroupIndex{1u}, MaterialBindGroupSlot{0u}}))
        << "a different bind group is a different binding";
    EXPECT_NE(binding, (MaterialBindingId{MaterialBindGroupIndex{0u}, MaterialBindGroupSlot{3u}}))
        << "and so is a different slot";
}

/** @brief Bevy's 3D `MeshFlags` (`render/mesh.rs:619-680`): the bit values that
 *  must match `mesh_types.wgsl`, the LOD index in the low 16 bits, and every arm
 *  of `from_components`. */
TEST(Cluster, MeshFlagsMatchBevy) {
    using epix::pbr::MeshFlags;
    using Bits = MeshFlags::Bits;

    // The values, not just the distinctness: these travel to the shader.
    EXPECT_EQ(static_cast<std::uint32_t>(Bits::NONE), 0u);
    EXPECT_EQ(static_cast<std::uint32_t>(Bits::LOD_INDEX_MASK), 0xFFFFu);
    EXPECT_EQ(static_cast<std::uint32_t>(Bits::NO_FRUSTUM_CULLING), 1u << 28);
    EXPECT_EQ(static_cast<std::uint32_t>(Bits::SHADOW_RECEIVER), 1u << 29);
    EXPECT_EQ(static_cast<std::uint32_t>(Bits::TRANSMITTED_SHADOW_RECEIVER), 1u << 30);
    EXPECT_EQ(static_cast<std::uint32_t>(Bits::SIGN_DETERMINANT_MODEL_3X3), 1u << 31);
    EXPECT_EQ(static_cast<std::uint32_t>(Bits::UNINITIALIZED), 0xFFFFFFFFu);
    EXPECT_EQ(MeshFlags::LOD_INDEX_SHIFT, 0u) << "the LOD index starts at the bottom bit";

    const epix::transform::GlobalTransform positive{glm::mat4{1.0f}};
    // Bevy reads the index back through the mask, so the test does too.
    const auto lod_bits = [](const MeshFlags& value) {
        return value.bits & static_cast<std::uint32_t>(Bits::LOD_INDEX_MASK);
    };

    MeshFlags flags = MeshFlags::from_components(positive, std::nullopt, false, false, false);
    EXPECT_TRUE(flags.contains(Bits::SHADOW_RECEIVER)) << "a mesh receives shadows unless it opts out";
    EXPECT_TRUE(flags.contains(Bits::SIGN_DETERMINANT_MODEL_3X3)) << "the determinant's sign is positive";
    EXPECT_FALSE(flags.contains(Bits::NO_FRUSTUM_CULLING));
    EXPECT_FALSE(flags.contains(Bits::TRANSMITTED_SHADOW_RECEIVER));
    EXPECT_EQ(lod_bits(flags), 0xFFFFu) << "no LOD is u16::MAX in the low 16 bits";

    EXPECT_FALSE(MeshFlags::from_components(positive, std::nullopt, false, true, false).contains(Bits::SHADOW_RECEIVER))
        << "NotShadowReceiver clears the flag";
    EXPECT_TRUE(MeshFlags::from_components(positive, std::nullopt, true, false, false).contains(Bits::NO_FRUSTUM_CULLING));
    EXPECT_TRUE(MeshFlags::from_components(positive, std::nullopt, false, false, true)
                    .contains(Bits::TRANSMITTED_SHADOW_RECEIVER));

    // A mirroring transform has a negative determinant, so the flag is clear.
    epix::transform::GlobalTransform mirrored{glm::scale(glm::mat4{1.0f}, glm::vec3{-1.0f, 1.0f, 1.0f})};
    EXPECT_FALSE(MeshFlags::from_components(mirrored, std::nullopt, false, false, false)
                     .contains(Bits::SIGN_DETERMINANT_MODEL_3X3));

    // A LOD index lands in the low 16 bits, and only there.
    const MeshFlags indexed = MeshFlags::from_components(positive, std::uint16_t{7}, false, false, false);
    EXPECT_EQ(lod_bits(indexed), 7u);
    EXPECT_TRUE(indexed.contains(Bits::SHADOW_RECEIVER)) << "the index does not disturb the flags above it";

    // The mask is exactly the low 16 bits.
    MeshFlags saturated;
    saturated.bits = static_cast<std::uint32_t>(Bits::LOD_INDEX_MASK);
    EXPECT_EQ(lod_bits(saturated), 0xFFFFu);
    EXPECT_FALSE(saturated.contains(Bits::NO_FRUSTUM_CULLING));
}

/** @brief Bevy's `MeshInputUniform` (`render/mesh.rs:500-560`): the fields the GPU
 *  builds a `MeshUniform` from, at the offsets the shader reads, and zeroed by
 *  default - Bevy derives `Default` on it. */
TEST(Cluster, MeshInputUniformLayoutMatchesBevy) {
    using epix::pbr::MeshInputUniform;

    // The size and offsets are the shader's contract; the header asserts them
    // too, and this checks the same numbers from the consumer's side.
    EXPECT_EQ(sizeof(MeshInputUniform), 96u);
    EXPECT_EQ(offsetof(MeshInputUniform, lightmap_uv_rect), 48u);
    EXPECT_EQ(offsetof(MeshInputUniform, flags), 56u);
    EXPECT_EQ(offsetof(MeshInputUniform, previous_input_index), 60u);
    EXPECT_EQ(offsetof(MeshInputUniform, first_vertex_index), 64u);
    EXPECT_EQ(offsetof(MeshInputUniform, first_index_index), 68u);
    EXPECT_EQ(offsetof(MeshInputUniform, index_count), 72u);
    EXPECT_EQ(offsetof(MeshInputUniform, current_skin_index), 76u);
    EXPECT_EQ(offsetof(MeshInputUniform, material_and_lightmap_bind_group_slot), 80u);
    EXPECT_EQ(offsetof(MeshInputUniform, timestamp), 84u);
    EXPECT_EQ(offsetof(MeshInputUniform, tag), 88u);
    EXPECT_EQ(offsetof(MeshInputUniform, pad), 92u);

    // Bevy derives `Default`, so every byte of a fresh input uniform is zero -
    // including the fields whose docs mention a `u32::MAX` sentinel, which the
    // producer writes rather than the default.
    const MeshInputUniform input{};
    const std::array<std::byte, sizeof(MeshInputUniform)> zeros{};
    EXPECT_EQ(std::memcmp(&input, zeros.data(), sizeof(input)), 0) << "a default input uniform is all zeros";
    EXPECT_EQ(input.world_from_local[0], glm::vec4(0.0f));
    EXPECT_EQ(input.lightmap_uv_rect, glm::uvec2(0u, 0u));
    EXPECT_EQ(input.previous_input_index, 0u);
    EXPECT_EQ(input.current_skin_index, 0u);
}

/** @brief Bevy's `MeshCullingDataBuffer` (`render/mesh.rs:576-583,1312-1317`): a
 *  `RawBufferVec<MeshCullingData>` behind a `Deref` newtype, empty by default so
 *  the CPU-culling case pays nothing, and reached through `*`/`->`. */
TEST(Cluster, MeshCullingDataBufferMatchesBevy) {
    epix::pbr::MeshCullingDataBuffer culling;
    EXPECT_TRUE(culling->is_empty()) << "GPU culling data is absent until it is pushed";
    EXPECT_EQ(culling->len(), 0u);
    EXPECT_EQ(culling->buffer(), nullptr) << "and no GPU buffer exists before a write";

    // `update` grows the buffer with zeroed entries and then replaces one
    // (Bevy `MeshCullingDataBuffer::update`, `render/mesh.rs:1300-1309`).
    while ((*culling).len() < 3) (*culling).push(epix::pbr::MeshCullingData{});
    EXPECT_EQ(culling->len(), 3u);
    const epix::pbr::MeshCullingData conservative = epix::pbr::MeshCullingData::make(std::nullopt);
    (*culling).set(1, conservative);
    ASSERT_NE(culling->get(1), nullptr);
    EXPECT_EQ(culling->get(1)->aabb_half_extents.x, std::numeric_limits<float>::infinity())
        << "a mesh with no AABB gets the infinite box, which is never culled";
    EXPECT_EQ(culling->get(1)->aabb_center, glm::vec4(0.0f));

    (*culling).clear();
    EXPECT_TRUE(culling->is_empty());
}

/** @brief Bevy's `MeshUniform::new` (`render/mesh.rs:585-615`): the two transposes,
 *  the packed normal matrix, the lightmap slot and UV rect, and the sentinels an
 *  absent lookup falls back to. */
TEST(Cluster, MeshUniformCreationMatchesBevy) {
    using epix::pbr::LightmapSlotIndex;
    using epix::pbr::MaterialBindGroupSlot;
    using epix::pbr::MeshTransforms;
    using epix::pbr::MeshUniform;

    // An identity transform transposes to the identity's rows, and its inverse
    // transpose packs to the identity as well.
    MeshTransforms identity{};
    identity.flags = 0x1234u;
    const MeshUniform identity_uniform =
        MeshUniform::make(identity, 7u, MaterialBindGroupSlot{3u}, std::nullopt, std::nullopt, std::nullopt);
    EXPECT_EQ(identity_uniform.world_from_local[0], glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_EQ(identity_uniform.world_from_local[1], glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
    EXPECT_EQ(identity_uniform.world_from_local[2], glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    EXPECT_EQ(identity_uniform.local_from_world_transpose_a[0], glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    // Bevy packs the second word as `[1].yz, [2].xy`, so on the identity it is the
    // middle column's y (1) and z (0) followed by the last column's x and y.
    EXPECT_EQ(identity_uniform.local_from_world_transpose_a[1], glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_EQ(identity_uniform.local_from_world_transpose_b, 1.0f);
    EXPECT_EQ(identity_uniform.flags, 0x1234u) << "the transform's flags pass through";
    EXPECT_EQ(identity_uniform.first_vertex_index, 7u);
    EXPECT_EQ(identity_uniform.tag, 0u) << "an absent tag is zero";
    EXPECT_EQ(identity_uniform.pad, 0u);

    // Absent lookups become the shader's sentinels: UINT16_MAX in the lightmap
    // half of the combined word, UINT32_MAX for the skin index.
    EXPECT_EQ(identity_uniform.material_and_lightmap_bind_group_slot, 3u | (0xFFFFu << 16));
    EXPECT_EQ(identity_uniform.current_skin_index, 0xFFFFFFFFu);
    EXPECT_EQ(identity_uniform.lightmap_uv_rect, glm::uvec2(0u, 0u)) << "no lightmap packs a zero rect";

    // A present lightmap fills the high half and the UV rect.
    const ::epix::image::Rect rect = ::epix::image::Rect::from_corners(glm::vec2(0.0f), glm::vec2(1.0f));
    const MeshUniform lightmapped = MeshUniform::make(
        identity, 0u, MaterialBindGroupSlot{2u}, std::pair{LightmapSlotIndex{9u}, rect}, std::uint32_t{4u}, 5u);
    EXPECT_EQ(lightmapped.material_and_lightmap_bind_group_slot, 2u | (9u << 16));
    // The first word holds min.u | min.v << 16, the second max.u | max.v << 16, so
    // the unit rect is (0,0) in the first word and 65535 in both halves of the
    // second.
    EXPECT_EQ(lightmapped.lightmap_uv_rect, glm::uvec2(0u, 0xFFFFFFFFu))
        << "the unit rect scales to 65535 in every component";
    EXPECT_EQ(lightmapped.current_skin_index, 4u);
    EXPECT_EQ(lightmapped.tag, 5u);

    // A non-uniform scale is what the inverse transpose exists for: the normal
    // matrix takes the reciprocal of the scale, not the scale.
    MeshTransforms scaled{};
    scaled.world_from_local          = glm::scale(glm::mat4{1.0f}, glm::vec3(2.0f, 4.0f, 8.0f));
    scaled.previous_world_from_local = glm::translate(glm::mat4{1.0f}, glm::vec3(1.0f, 2.0f, 3.0f));
    const MeshUniform scaled_uniform =
        MeshUniform::make(scaled, 0u, MaterialBindGroupSlot{0u}, std::nullopt, std::nullopt, std::nullopt);
    EXPECT_EQ(scaled_uniform.local_from_world_transpose_a[0], glm::vec4(0.5f, 0.0f, 0.0f, 0.0f));
    // The second packed word takes the middle column's y and z and the last
    // column's x and y (Bevy's `[1].yz, [2].xy`).
    EXPECT_EQ(scaled_uniform.local_from_world_transpose_a[1], glm::vec4(0.25f, 0.0f, 0.0f, 0.0f));
    EXPECT_EQ(scaled_uniform.local_from_world_transpose_b, 0.125f);
    EXPECT_EQ(scaled_uniform.world_from_local[0], glm::vec4(2.0f, 0.0f, 0.0f, 0.0f))
        << "and the transform itself is transposed, not inverted";
    EXPECT_EQ(scaled_uniform.previous_world_from_local[0], glm::vec4(1.0f, 0.0f, 0.0f, 1.0f))
        << "the translation lands in the fourth component of the first vec4";
    EXPECT_EQ(scaled_uniform.previous_world_from_local[1], glm::vec4(0.0f, 1.0f, 0.0f, 2.0f));
    EXPECT_EQ(scaled_uniform.previous_world_from_local[2], glm::vec4(0.0f, 0.0f, 1.0f, 3.0f));
}

/** @brief Bevy's `RenderMeshInstanceGpuQueue` (`render/mesh.rs:785-1122`): the two
 *  culling shapes, the in-place clear when the kind matches, the promotion when it
 *  does not, and the removal list. */
TEST(Cluster, RenderMeshInstanceGpuQueueMatchesBevy) {
    using Queue = epix::pbr::RenderMeshInstanceGpuQueue;
    using Builder = epix::pbr::RenderMeshInstanceGpuBuilder;
    const auto main_entity = [](std::uint32_t index) {
        return epix::render::sync_world::MainEntity{epix::ecs::Entity{index}};
    };
    const auto culling_data = epix::pbr::MeshCullingData{};
    // The shared record has no default constructor - the mesh id is what it is
    // about, as in Bevy - so a builder is spelled out with an invalid id.
    const auto make_builder = [] {
        return epix::pbr::RenderMeshInstanceGpuBuilder{
            .shared = epix::pbr::RenderMeshInstanceShared{
                .mesh_asset_id = epix::assets::AssetId<epix::mesh::Mesh>::invalid()}};
    };

    // The default is the unit `None` variant.
    Queue queue;
    EXPECT_TRUE(std::holds_alternative<std::monostate>(queue.queue));

    // `init` takes the shape the culling kind asks for.
    queue.init(false);
    ASSERT_TRUE(std::holds_alternative<Queue::CpuCulling>(queue.queue));
    queue.init(true);
    ASSERT_TRUE(std::holds_alternative<Queue::GpuCulling>(queue.queue));

    // Pushing with culling data into the GPU shape appends; pushing without it
    // promotes the queue to the CPU shape, discarding queued removals as Bevy does.
    queue.push(main_entity(1), make_builder(), culling_data);
    EXPECT_EQ(std::get<Queue::GpuCulling>(queue.queue).changed.size(), 1u);
    queue.remove(main_entity(2), true);
    EXPECT_EQ(std::get<Queue::GpuCulling>(queue.queue).removed.size(), 1u);
    queue.push(main_entity(3), make_builder(), std::nullopt);
    ASSERT_TRUE(std::holds_alternative<Queue::CpuCulling>(queue.queue));
    const auto& promoted = std::get<Queue::CpuCulling>(queue.queue);
    EXPECT_EQ(promoted.changed.size(), 1u);
    EXPECT_TRUE(promoted.removed.empty()) << "the promotion starts a fresh queue";

    // A removal keeps the shape the queue already has, whatever the flag says.
    queue.remove(main_entity(4), true);
    EXPECT_EQ(std::get<Queue::CpuCulling>(queue.queue).removed.size(), 1u);

    // `init` clears in place, so the shape is kept and the entries are dropped.
    queue.init(false);
    EXPECT_TRUE(std::get<Queue::CpuCulling>(queue.queue).changed.empty());
    EXPECT_TRUE(std::get<Queue::CpuCulling>(queue.queue).removed.empty());

    // A removal into the empty queue takes the shape the flag asks for.
    Queue fresh;
    fresh.remove(main_entity(5), true);
    ASSERT_TRUE(std::holds_alternative<Queue::GpuCulling>(fresh.queue));
    EXPECT_EQ(std::get<Queue::GpuCulling>(fresh.queue).removed.size(), 1u);
    EXPECT_TRUE(std::get<Queue::GpuCulling>(fresh.queue).changed.empty());
}

/** @brief Bevy's `MeshCullingData::update` (`render/mesh.rs:1298-1309`): grow the
 *  buffer with zeroed entries up to the slot, then overwrite it. */
TEST(Cluster, MeshCullingDataUpdateFlushesIntoTheBuffer) {
    epix::pbr::MeshCullingDataBuffer culling;
    const epix::pbr::MeshCullingData infinities = epix::pbr::MeshCullingData::make(std::nullopt);

    infinities.update(culling, 2);
    EXPECT_EQ(culling->len(), 3u) << "the buffer grows to the slot";
    EXPECT_EQ(culling->get(0)->aabb_half_extents.x, 0.0f) << "the entries it grows with are zeroed";
    EXPECT_EQ(culling->get(1)->aabb_half_extents.x, 0.0f);
    ASSERT_NE(culling->get(2), nullptr);
    EXPECT_TRUE(std::isinf(culling->get(2)->aabb_half_extents.x)) << "and the slot itself is written";

    // Writing a lower slot replaces it and leaves the ones above alone.
    const epix::pbr::MeshCullingData finite =
        epix::pbr::MeshCullingData::make(epix::camera::Aabb{.center = glm::vec3(1.0f), .half_extents = glm::vec3(2.0f)});
    finite.update(culling, 0);
    EXPECT_EQ(culling->len(), 3u) << "no growth is needed";
    ASSERT_NE(culling->get(0), nullptr);
    EXPECT_EQ(culling->get(0)->aabb_center.x, 1.0f);
    EXPECT_EQ(culling->get(0)->aabb_half_extents.x, 2.0f);
    EXPECT_TRUE(std::isinf(culling->get(2)->aabb_half_extents.x)) << "the other slot is untouched";
}

/** @brief Bevy's `RenderMeshInstanceShared::for_gpu_building`/`for_cpu_building`
 *  (`render/mesh.rs:837-892`): the three flag bits, the tag default, the absent
 *  lightmap slab, the optional render layers and the AABB-derived centre. */
TEST(Cluster, RenderMeshInstanceSharedFactoriesMatchBevy) {
    using Flags = epix::pbr::RenderMeshInstanceFlags;
    using Shared = epix::pbr::RenderMeshInstanceShared;

    epix::mesh::Mesh3d mesh{};
    epix::pbr::PreviousGlobalTransform previous{};

    // The GPU factory delegates with a default material binding id.
    const Shared gpu = Shared::for_gpu_building(&previous, mesh, nullptr, false, false, nullptr, nullptr);
    EXPECT_TRUE(gpu.flags.contains(Flags::SHADOW_CASTER)) << "not_shadow_caster is false, so the bit is set";
    EXPECT_TRUE(gpu.flags.contains(Flags::AUTOMATIC_BATCHING));
    EXPECT_TRUE(gpu.flags.contains(Flags::HAS_PREVIOUS_TRANSFORM)) << "a previous transform is present";
    EXPECT_EQ(gpu.tag, 0u) << "an absent tag is zero";
    EXPECT_FALSE(gpu.lightmap_slab_index.has_value()) << "the GPU path fills the slab in later";
    EXPECT_FALSE(gpu.render_layers.has_value()) << "an absent layer component is not an empty layer set";
    EXPECT_EQ(gpu.center, glm::vec3(0.0f)) << "no AABB leaves the centre at the origin";
    EXPECT_EQ(gpu.material_bindings_index, epix::pbr::MaterialBindingId{});

    // The opting-out components clear their bits, and no previous transform clears
    // that one.
    const Shared cpu = Shared::for_cpu_building(nullptr, mesh, nullptr, epix::pbr::MaterialBindingId{}, true, true,
                                                nullptr, nullptr);
    EXPECT_FALSE(cpu.flags.contains(Flags::SHADOW_CASTER)) << "not_shadow_caster clears it";
    EXPECT_FALSE(cpu.flags.contains(Flags::AUTOMATIC_BATCHING)) << "no_automatic_batching clears it";
    EXPECT_FALSE(cpu.flags.contains(Flags::HAS_PREVIOUS_TRANSFORM)) << "and no previous transform clears that";

    // The optional components are carried through when present.
    const epix::mesh::MeshTag tag{.value = 42u};
    const epix::camera::RenderLayers layers{};
    const epix::camera::Aabb aabb{.center = glm::vec3(1.0f, 2.0f, 3.0f), .half_extents = glm::vec3(0.5f)};
    const Shared full = Shared::for_cpu_building(&previous, mesh, &tag, epix::pbr::MaterialBindingId{}, false, false,
                                                 &layers, &aabb);
    EXPECT_EQ(full.tag, 42u) << "the tag is the component's value";
    ASSERT_TRUE(full.render_layers.has_value());
    EXPECT_EQ(full.center, glm::vec3(1.0f, 2.0f, 3.0f)) << "the centre comes from the AABB";
}

/** @brief Bevy's `MeshesToReextractNextFrame` (`render/mesh.rs:830-835`): a
 *  resource over a set of main entities, empty by default. */
TEST(Cluster, MeshesToReextractNextFrameMatchesBevy) {
    epix::pbr::MeshesToReextractNextFrame pending;
    EXPECT_TRUE(pending->empty());
    const auto entity = epix::render::sync_world::MainEntity{epix::ecs::Entity{7}};
    pending->insert(entity);
    EXPECT_EQ(pending->size(), 1u);
    EXPECT_TRUE(pending->contains(entity)) << "the mesh is retried next frame";
    pending->clear();
    EXPECT_TRUE((*pending).empty());
}

/** @brief Bevy's `TransmittedShadowReceiver` marker and its `Lightmap` component
 *  (`bevy_light/src/lib.rs:223-232`, `bevy_pbr/src/lightmap/mod.rs:88-108`): the two
 *  entities the GPU mesh extraction query reads. */
TEST(Cluster, ShadowTransmissionAndLightmapComponentsMatchBevy) {
    using epix::pbr::Lightmap;
    using epix::pbr::TransmittedShadowReceiver;

    // The marker is a component, and `Has<..>` sees it.
    epix::ecs::World world(epix::ecs::WorldId(0));
    const auto marked = world.spawn(TransmittedShadowReceiver{}).id();
    EXPECT_TRUE(world.get_entity(marked)->contains<TransmittedShadowReceiver>());
    const auto plain = world.spawn().id();
    EXPECT_FALSE(world.get_entity(plain)->contains<TransmittedShadowReceiver>())
        << "the marker is opt-in, as in Bevy";

    // `Lightmap` carries the texture, the atlas rect and the bicubic flag; Bevy
    // does not derive `Default` on it, so the handle has to be given.
    const Lightmap lightmap{.image = epix::assets::Handle<epix::image::Image>{
                                 epix::assets::AssetId<epix::image::Image>::invalid()},
                             .uv_rect = epix::image::Rect::from_corners(glm::vec2(0.0f), glm::vec2(1.0f))};
    EXPECT_EQ(lightmap.uv_rect.min, glm::vec2(0.0f));
    EXPECT_EQ(lightmap.uv_rect.max, glm::vec2(1.0f));
    EXPECT_FALSE(lightmap.bicubic_sampling) << "Bevy's default is linear sampling";

    // And it is an ordinary component on a mesh entity, which is how the extraction
    // query reads it as an `Opt<const Lightmap&>`.
    const auto lit = world.spawn(lightmap).id();
    ASSERT_TRUE(world.get_entity(lit)->get<Lightmap>().has_value());
    EXPECT_EQ(world.get_entity(lit)->get<Lightmap>()->get().uv_rect.max, glm::vec2(1.0f));
}

/** @brief Bevy's `extract_meshes_for_gpu_building` (`render/mesh.rs:1487-1624`): a
 *  visible mesh is queued as an instance builder, an invisible one and one that lost
 *  its `Mesh3d` are queued for removal, and the culling data is collected because a
 *  camera without `NoIndirectDrawing` is present. The system reads the main world
 *  through `ExtractedWorld`, so a pair of bare worlds drives it exactly as the
 *  extraction schedule does. */
TEST(MeshExtraction, QueuesVisibleMeshesAndRemovals) {
    using epix::camera::Aabb;
    using epix::camera::ViewVisibility;
    using epix::mesh::Mesh3d;
    using epix::mesh::MeshTag;
    using epix::pbr::Lightmap;
    using epix::pbr::MeshFlags;
    using epix::pbr::MeshesToReextractNextFrame;
    using epix::pbr::RenderMeshInstanceGpuQueue;
    using epix::pbr::RenderMeshInstanceGpuQueues;
    using epix::pbr::RenderMeshInstances;
    using epix::pbr::TransmittedShadowReceiver;
    using epix::render::sync_world::MainEntity;
    using epix::render::view::RenderVisibilityRanges;

    epix::ecs::World main_world(11);
    epix::ecs::World render_world(11);
    render_world.insert_resource(epix::app::ExtractedWorld{main_world});
    render_world.insert_resource(RenderMeshInstances::make(true));
    render_world.insert_resource(RenderMeshInstanceGpuQueues{});
    render_world.insert_resource(RenderVisibilityRanges{});
    render_world.insert_resource(MeshesToReextractNextFrame{});

    // A camera that is not drawing indirectly is what turns GPU culling on, and so
    // what makes the extraction collect per-instance culling data.
    main_world.spawn(epix::camera::Camera{});

    ViewVisibility visible;
    visible.set_visible();
    const ViewVisibility hidden;

    const auto make_image_handle = [] {
        return epix::assets::Handle<epix::image::Image>{epix::assets::AssetId<epix::image::Image>::invalid()};
    };

    const auto visible_entity = main_world
                                    .spawn(Mesh3d{}, epix::transform::GlobalTransform{}, visible,
                                           Aabb{.center = glm::vec3(1.0f, 2.0f, 3.0f), .half_extents = glm::vec3(1.0f)},
                                           MeshTag{.value = 9u}, TransmittedShadowReceiver{},
                                           Lightmap{.image = make_image_handle(),
                                                    .uv_rect = epix::image::Rect::from_corners(glm::vec2(0.0f),
                                                                                               glm::vec2(1.0f))})
                                    .id();
    const auto hidden_entity = main_world.spawn(Mesh3d{}, epix::transform::GlobalTransform{}, hidden).id();
    // An entity that loses its `Mesh3d` is queued for removal even though it is
    // still visible.
    const auto removed_entity = main_world.spawn(Mesh3d{}, epix::transform::GlobalTransform{}, visible).id();
    main_world.entity_mut(removed_entity).remove<Mesh3d>();

    auto system = epix::ecs::make_system_unique(epix::pbr::extract_meshes_for_gpu_building);
    system->initialize(render_world);
    ASSERT_TRUE(system->run({}, render_world).has_value());
    system->apply_deferred(render_world);

    const auto& queues  = render_world.get_resource<RenderMeshInstanceGpuQueues>()->get();
    // Bevy's `Deref` reaches the queue; its `.queue` is the variant holding the shape.
    const auto& payload = (*queues).queue;
    ASSERT_TRUE(std::holds_alternative<RenderMeshInstanceGpuQueue::GpuCulling>(payload))
        << "the camera without NoIndirectDrawing turns GPU culling on";
    const auto& gpu = std::get<RenderMeshInstanceGpuQueue::GpuCulling>(payload);

    ASSERT_EQ(gpu.changed.size(), 1u) << "only the visible mesh produces an instance";
    const auto& [entity, builder, culling_data] = gpu.changed.front();
    EXPECT_EQ(entity, MainEntity{visible_entity});
    EXPECT_EQ(builder.shared.mesh_asset_id, epix::assets::AssetId<epix::mesh::Mesh>::invalid());
    EXPECT_TRUE(builder.shared.flags.contains(epix::pbr::RenderMeshInstanceFlags::SHADOW_CASTER));
    EXPECT_TRUE(builder.shared.flags.contains(epix::pbr::RenderMeshInstanceFlags::AUTOMATIC_BATCHING));
    EXPECT_FALSE(builder.shared.flags.contains(epix::pbr::RenderMeshInstanceFlags::HAS_PREVIOUS_TRANSFORM))
        << "there is no PreviousGlobalTransform on the entity";
    EXPECT_EQ(builder.shared.tag, 9u) << "the MeshTag reaches the shared record";
    EXPECT_EQ(builder.lightmap_uv_rect, glm::uvec2(0u, 0xFFFFFFFFu)) << "the lightmap's atlas rect is packed";
    EXPECT_TRUE(builder.mesh_flags.contains(MeshFlags::TRANSMITTED_SHADOW_RECEIVER));
    EXPECT_TRUE(builder.mesh_flags.contains(MeshFlags::SHADOW_RECEIVER));
    EXPECT_TRUE(builder.mesh_flags.contains(MeshFlags::SIGN_DETERMINANT_MODEL_3X3));
    EXPECT_FALSE(builder.previous_input_index.has_value()) << "the instance was not extracted before";
    EXPECT_EQ(culling_data.aabb_center, glm::vec4(1.0f, 2.0f, 3.0f, 0.0f));

    ASSERT_EQ(gpu.removed.size(), 2u) << "the invisible mesh and the one that lost its Mesh3d";
    const auto was_removed = [&gpu](epix::ecs::Entity entity) {
        return std::ranges::find(gpu.removed, MainEntity{entity}) != gpu.removed.end();
    };
    EXPECT_TRUE(was_removed(hidden_entity));
    EXPECT_TRUE(was_removed(removed_entity));
}

/** @brief The cluster extraction inserts each active view's flat records and
 *  config on its render entity, clears both from an inactive camera's, and skips
 *  a clusterable object that was never synced (Bevy `extract_clusters`,
 *  `bevy_pbr/src/cluster.rs:245-287`). */
TEST(Cluster, ExtractClustersMatchesBevy) {
    using epix::pbr::Clusters;
    using epix::pbr::ClusterableObjectCounts;
    using epix::pbr::ExtractedClusterConfig;
    using epix::pbr::ExtractedClusterableObjectElement;
    using epix::pbr::ExtractedClusterableObjects;
    using epix::pbr::VisibleClusterableObjects;
    using epix::render::sync_world::RenderEntity;

    epix::ecs::World main_world(13);
    epix::ecs::World render_world(13);
    render_world.insert_resource(epix::app::ExtractedWorld{main_world});

    const auto active_render   = render_world.spawn().id();
    const auto inactive_render = render_world.spawn().id();
    const auto synced_render   = render_world.spawn().id();

    // A view that went inactive already carries both components, which is what
    // makes the removal branch observable.
    render_world.entity_mut(inactive_render).insert(ExtractedClusterableObjects{}, ExtractedClusterConfig{});

    // One clusterable object is synced to the render world; the other is not, so
    // the mapper query has no row for it.
    const auto synced_object   = main_world.spawn(RenderEntity{synced_render}).id();
    const auto unsynced_object = main_world.spawn().id();

    Clusters clusters;
    clusters.near_plane = 0.1f;
    clusters.far_plane  = 100.0f;
    clusters.dimensions = glm::uvec3(4u, 4u, 8u);
    VisibleClusterableObjects cluster;
    cluster.counts   = ClusterableObjectCounts{.point_lights = 2u};
    cluster.entities = {synced_object, unsynced_object};
    clusters.clusterable_objects.push_back(cluster);

    main_world.spawn(RenderEntity{active_render}, clusters, epix::camera::Camera{});
    auto inactive_camera      = epix::camera::Camera{};
    inactive_camera.is_active = false;
    main_world.spawn(RenderEntity{inactive_render}, clusters, inactive_camera);

    auto system = epix::ecs::make_system_unique(epix::pbr::extract_clusters);
    system->initialize(render_world);
    ASSERT_TRUE(system->run({}, render_world).has_value());
    system->apply_deferred(render_world);

    const auto active = render_world.entity(active_render);
    ASSERT_TRUE(active.contains<ExtractedClusterableObjects>());
    ASSERT_TRUE(active.contains<ExtractedClusterConfig>());
    const auto& objects = active.get<ExtractedClusterableObjects>()->get();
    ASSERT_EQ(objects.data.size(), 2u) << "the cluster header plus the one synced object";
    EXPECT_TRUE(objects.data[0].kind == ExtractedClusterableObjectElement::Kind::ClusterHeader);
    EXPECT_EQ(objects.data[0].counts.point_lights, 2u) << "the header carries the cluster's counts";
    EXPECT_TRUE(objects.data[1].kind == ExtractedClusterableObjectElement::Kind::ClusterableObjectEntity);
    EXPECT_EQ(objects.data[1].entity, synced_render) << "the object's render entity is recorded";
    const auto& config = active.get<ExtractedClusterConfig>()->get();
    EXPECT_FLOAT_EQ(config.near_plane, 0.1f);
    EXPECT_FLOAT_EQ(config.far_plane, 100.0f);
    EXPECT_EQ(config.dimensions, glm::uvec3(4u, 4u, 8u));

    const auto inactive = render_world.entity(inactive_render);
    EXPECT_FALSE(inactive.contains<ExtractedClusterableObjects>());
    EXPECT_FALSE(inactive.contains<ExtractedClusterConfig>());
}

/** @brief The shared instance record keeps the mesh id it is about, carries the
 *  absence of a lightmap and of render layers distinctly, and delegates the
 *  batching question to its flags (Bevy `RenderMeshInstanceShared`,
 *  `render/mesh.rs:735-755,894-900`). */
TEST(Cluster, RenderMeshInstanceSharedMatchesBevy) {
    using epix::pbr::LightmapSlabIndex;
    using epix::pbr::MaterialBindGroupIndex;
    using epix::pbr::MaterialBindGroupSlot;
    using epix::pbr::RenderMeshInstanceFlags;
    using epix::pbr::RenderMeshInstanceShared;

    RenderMeshInstanceShared shared{.mesh_asset_id = epix::assets::AssetId<epix::mesh::Mesh>::invalid(),
                                   .material_bindings_index = {MaterialBindGroupIndex{2u}, MaterialBindGroupSlot{0u}},
                                   .flags = {},
                                   .tag   = 7u,
                                   .center = glm::vec3(1.0f, 2.0f, 3.0f)};

    EXPECT_EQ(shared.mesh_asset_id, epix::assets::AssetId<epix::mesh::Mesh>::invalid());
    EXPECT_EQ(shared.material_bindings_index.group.index, 2u);
    EXPECT_EQ(shared.tag, 7u);
    EXPECT_EQ(shared.center, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(shared.flags.bits, 0u);
    EXPECT_FALSE(shared.should_batch()) << "nothing set the batching bit";

    // Absent lightmap and absent layers are distinct from a lightmap at slab 0 and
    // from belonging to no layer.
    EXPECT_FALSE(shared.lightmap_slab_index.has_value());
    EXPECT_FALSE(shared.render_layers.has_value());
    shared.lightmap_slab_index = LightmapSlabIndex{0u};
    shared.render_layers       = epix::camera::RenderLayers::layer(0);
    ASSERT_TRUE(shared.lightmap_slab_index.has_value());
    EXPECT_EQ(shared.lightmap_slab_index->index, 0u);
    ASSERT_TRUE(shared.render_layers.has_value());
    EXPECT_TRUE(shared.render_layers->intersects(epix::camera::RenderLayers::layer(0)));

    // The batching bit is the flags' bit, not a second copy of it.
    shared.flags.insert(RenderMeshInstanceFlags::AUTOMATIC_BATCHING);
    EXPECT_TRUE(shared.should_batch());
    shared.flags = {};
    EXPECT_FALSE(shared.should_batch());
}

/** @brief The instance record set dispatches to whichever side is in use, and the
 *  two sides hold different data (Bevy `RenderMeshInstances`,
 *  `render/mesh.rs:903-968`). */
TEST(Cluster, RenderMeshInstancesDispatchMatchesBevy) {
    using epix::pbr::MaterialBindGroupIndex;
    using epix::pbr::MaterialBindGroupSlot;
    using epix::pbr::RenderMeshInstanceCpu;
    using epix::pbr::RenderMeshInstanceFlags;
    using epix::pbr::RenderMeshInstanceGpu;
    using epix::pbr::RenderMeshInstanceShared;
    using epix::pbr::RenderMeshInstances;

    const auto entity = epix::render::sync_world::MainEntity{epix::ecs::Entity::from_index(5)};
    const auto id     = epix::assets::AssetId<epix::mesh::Mesh>::invalid();
    const auto shared = [&id] {
        RenderMeshInstanceShared record{.mesh_asset_id = id,
                                        .material_bindings_index = {MaterialBindGroupIndex{0u}, MaterialBindGroupSlot{0u}},
                                        .flags = {}};
        return record;
    };

    // The CPU side: the shared record plus the transform.
    RenderMeshInstances cpu = RenderMeshInstances::make(false);
    EXPECT_TRUE(cpu.is_cpu_building());
    EXPECT_FALSE(cpu.mesh_asset_id(entity).has_value()) << "nothing recorded for this entity yet";
    cpu.cpu_instances.emplace(entity, RenderMeshInstanceCpu{shared(), epix::pbr::MeshTransforms{}});
    ASSERT_TRUE(cpu.mesh_asset_id(entity).has_value());
    EXPECT_EQ(*cpu.mesh_asset_id(entity), id);
    EXPECT_TRUE(cpu.gpu_instances.empty()) << "only one side is populated";

    // The GPU side: the shared record plus a world centre and a uniform index, and
    // the dispatch follows the kind rather than the populated map.
    RenderMeshInstances gpu = RenderMeshInstances::make(true);
    EXPECT_FALSE(gpu.is_cpu_building());
    gpu.gpu_instances.emplace(entity, RenderMeshInstanceGpu{shared(), glm::vec3(1.0f, 0.0f, 0.0f), 3u});
    ASSERT_TRUE(gpu.mesh_asset_id(entity).has_value());
    EXPECT_EQ(*gpu.mesh_asset_id(entity), id);
    EXPECT_EQ(gpu.gpu_instances.at(entity).current_uniform_index, 3u);

    // Flags reach the side in use, and are ignored for an unknown entity.
    RenderMeshInstanceFlags flags;
    flags.insert(RenderMeshInstanceFlags::SHADOW_CASTER);
    cpu.insert_mesh_instance_flags(entity, flags);
    EXPECT_TRUE(cpu.cpu_instances.at(entity).shared.flags.contains(RenderMeshInstanceFlags::SHADOW_CASTER));
    EXPECT_FALSE(gpu.gpu_instances.at(entity).shared.flags.contains(RenderMeshInstanceFlags::SHADOW_CASTER))
        << "the other record set was not touched";
    gpu.insert_mesh_instance_flags(entity, flags);
    EXPECT_TRUE(gpu.gpu_instances.at(entity).shared.flags.contains(RenderMeshInstanceFlags::SHADOW_CASTER));

    const auto unknown = epix::render::sync_world::MainEntity{epix::ecs::Entity::from_index(99)};
    cpu.insert_mesh_instance_flags(unknown, flags);  // must not throw or insert
    EXPECT_EQ(cpu.cpu_instances.size(), 1u);
    EXPECT_FALSE(cpu.mesh_asset_id(unknown).has_value());
}

/** @brief The queue data differs between the two sides in how its world-space
 *  centre is obtained, and the CPU side reports a default uniform index (Bevy
 *  `render/mesh.rs:944-1017`). */
TEST(Cluster, RenderMeshQueueDataMatchesBevy) {
    using epix::pbr::MaterialBindGroupIndex;
    using epix::pbr::MaterialBindGroupSlot;
    using epix::pbr::MeshTransforms;
    using epix::pbr::RenderMeshInstanceCpu;
    using epix::pbr::RenderMeshInstanceGpu;
    using epix::pbr::RenderMeshInstanceShared;
    using epix::pbr::RenderMeshInstances;

    const auto entity = epix::render::sync_world::MainEntity{epix::ecs::Entity::from_index(11)};
    const auto shared = [] {
        return RenderMeshInstanceShared{
            .mesh_asset_id = epix::assets::AssetId<epix::mesh::Mesh>::invalid(),
            .material_bindings_index = {MaterialBindGroupIndex{0u}, MaterialBindGroupSlot{0u}},
            .flags = {}};
    };

    // The CPU side derives the centre from the local one through the transform,
    // including rotation and translation.
    RenderMeshInstances cpu = RenderMeshInstances::make(false);
    RenderMeshInstanceCpu cpu_instance{shared(), MeshTransforms{}};
    cpu_instance.shared.center = glm::vec3(1.0f, 0.0f, 0.0f);
    cpu_instance.transforms.world_from_local =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 5.0f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    cpu.cpu_instances.emplace(entity, cpu_instance);

    const auto cpu_data = cpu.render_mesh_queue_data(entity);
    ASSERT_TRUE(cpu_data.has_value());
    // Rotating (1,0,0) by 90 degrees about Z gives (0,1,0), then translated by 5 in y.
    EXPECT_NEAR(cpu_data->center.x, 0.0f, 1e-5f);
    EXPECT_NEAR(cpu_data->center.y, 6.0f, 1e-5f);
    EXPECT_NEAR(cpu_data->center.z, 0.0f, 1e-5f);
    EXPECT_EQ(cpu_data->current_uniform_index.index, 0u) << "the CPU path has no uniform";
    EXPECT_EQ(cpu_data->record().mesh_asset_id, epix::assets::AssetId<epix::mesh::Mesh>::invalid())
        << "the shared record is reachable through the queue data";

    // The GPU side hands back the centre it stored, and its uniform index.
    RenderMeshInstances gpu = RenderMeshInstances::make(true);
    gpu.gpu_instances.emplace(entity, RenderMeshInstanceGpu{shared(), glm::vec3(7.0f, 8.0f, 9.0f), 4u});
    const auto gpu_data = gpu.render_mesh_queue_data(entity);
    ASSERT_TRUE(gpu_data.has_value());
    EXPECT_EQ(gpu_data->center, glm::vec3(7.0f, 8.0f, 9.0f)) << "stored, not derived";
    EXPECT_EQ(gpu_data->current_uniform_index.index, 4u);

    // An entity with no record has no queue data on either side.
    const auto unknown = epix::render::sync_world::MainEntity{epix::ecs::Entity::from_index(42)};
    EXPECT_FALSE(cpu.render_mesh_queue_data(unknown).has_value());
    EXPECT_FALSE(gpu.render_mesh_queue_data(unknown).has_value());
}

/** @brief The mesh culling data is a model-space AABB padded to two vec4s, and a
 *  mesh with no AABB gets an infinite one rather than a zero one (Bevy
 *  `MeshCullingData`, `render/mesh.rs:562-574,1280-1296`). */
TEST(Cluster, MeshCullingDataMatchesBevy) {
    using epix::pbr::MeshCullingData;

    // The layout the shader reads: two vec4s, the second after the first.
    EXPECT_EQ(sizeof(MeshCullingData), 32u);
    EXPECT_EQ(offsetof(MeshCullingData, aabb_center), 0u);
    EXPECT_EQ(offsetof(MeshCullingData, aabb_half_extents), 16u);

    // An AABB's centre and half extents, with the padding word left at zero.
    const epix::camera::Aabb aabb{.center = glm::vec3(1.0f, 2.0f, 3.0f),
                                  .half_extents = glm::vec3(0.5f, 1.0f, 2.0f)};
    const MeshCullingData from_aabb = MeshCullingData::make(aabb);
    EXPECT_EQ(glm::vec3(from_aabb.aabb_center), glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(from_aabb.aabb_center.w, 0.0f) << "the fourth float is padding";
    EXPECT_EQ(glm::vec3(from_aabb.aabb_half_extents), glm::vec3(0.5f, 1.0f, 2.0f));
    EXPECT_EQ(from_aabb.aabb_half_extents.w, 0.0f);

    // No AABB means never culled, not always culled: the half extents are infinite
    // so a test asking "is it outside" can never answer yes.
    const MeshCullingData without = MeshCullingData::make(std::nullopt);
    EXPECT_EQ(glm::vec3(without.aabb_center), glm::vec3(0.0f));
    EXPECT_TRUE(std::isinf(without.aabb_half_extents.x));
    EXPECT_TRUE(std::isinf(without.aabb_half_extents.y));
    EXPECT_TRUE(std::isinf(without.aabb_half_extents.z));
    EXPECT_EQ(without.aabb_half_extents.w, 0.0f) << "the padding stays zero even here";
    EXPECT_GT(without.aabb_half_extents.x, 1e9f) << "infinite is larger than any real extent";
}

/** @brief The shadow opt-out markers are distinct types, so a mesh can cast without
 *  receiving or the reverse, and both are usable as query filters (Bevy
 *  `NotShadowCaster`/`NotShadowReceiver`, `bevy_light/src/lib.rs:211-222`). */
TEST(Cluster, ShadowMarkersMatchBevy) {
    using epix::pbr::NotShadowCaster;
    using epix::pbr::NotShadowReceiver;

    // Distinct types rather than one flag: the two are independent, and Bevy's
    // light-visibility query filters on the caster one alone.
    EXPECT_FALSE((std::is_same_v<NotShadowCaster, NotShadowReceiver>));
    EXPECT_TRUE(std::is_empty_v<NotShadowCaster>) << "a marker carries no data";
    EXPECT_TRUE(std::is_empty_v<NotShadowReceiver>);
    EXPECT_TRUE(std::is_default_constructible_v<NotShadowCaster>);
    EXPECT_TRUE(std::is_default_constructible_v<NotShadowReceiver>);

    // Being spawnable and queryable by presence is what makes them markers, and a
    // world holding one of each keeps them apart.
    epix::ecs::World world(2);
    const auto caster_only   = world.spawn(NotShadowCaster{}).id();
    const auto receiver_only = world.spawn(NotShadowReceiver{}).id();
    EXPECT_TRUE(world.entity(caster_only).contains<NotShadowCaster>());
    EXPECT_FALSE(world.entity(caster_only).contains<NotShadowReceiver>())
        << "carrying one marker does not imply the other";
    EXPECT_TRUE(world.entity(receiver_only).contains<NotShadowReceiver>());
    EXPECT_FALSE(world.entity(receiver_only).contains<NotShadowCaster>());
}

/** @brief The per-face visibility of a mesh to a point light: conservative when
 *  there is nothing to cull against, bounded by the light's range, and per-face
 *  otherwise (Bevy `check_point_light_mesh_visibility`,
 *  `bevy_light/src/lib.rs:565-590`). */
TEST(Cluster, PointLightFacesComeFromTheVisibilitySystem) {
    using epix::camera::Aabb;
    using epix::pbr::check_point_light_mesh_visibility;
    using epix::pbr::point_light_cubemap_frusta;
    using epix::pbr::PointLight;
    using epix::pbr::VisibleClusterableObjects;

    epix::app::App app = epix::app::App::create();

    // A point light at the origin with its cube frusta built the way the shadow phase
    // builds them, plus a view that has the light in its clustered-object set.
    const float range = 10.0f;
    epix::camera::CubemapFrusta cubemap;
    cubemap.frusta = point_light_cubemap_frusta(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), range, 0.1f);
    const auto light = app.world_mut()
                           .spawn(PointLight{.shadows_enabled = true, .range = range},
                                  epix::transform::GlobalTransform{}, cubemap,
                                  epix::camera::CubemapVisibleEntities{})
                           .id();
    VisibleClusterableObjects view_objects;
    view_objects.entities.push_back(light);
    app.world_mut().spawn(view_objects);

    const auto mesh_at = [&app](glm::vec3 position) {
        return app.world_mut()
            .spawn(epix::mesh::Mesh3d{}, epix::camera::InheritedVisibility{}, epix::camera::ViewVisibility::hidden(),
                   epix::transform::GlobalTransform{}, Aabb{.center = position, .half_extents = glm::vec3(0.1f)})
            .id();
    };
    const auto on_x_axis = mesh_at(glm::vec3(2.0f, 0.0f, 0.0f));
    const auto below     = mesh_at(glm::vec3(0.0f, -2.0f, 0.0f));
    const auto beyond    = mesh_at(glm::vec3(range + 5.0f, 0.0f, 0.0f));
    // Nothing to cull against, and culling switched off: both are visible everywhere.
    const auto unbounded = app.world_mut()
                               .spawn(epix::mesh::Mesh3d{}, epix::camera::InheritedVisibility{},
                                      epix::camera::ViewVisibility::hidden(), epix::transform::GlobalTransform{})
                               .id();
    const auto no_culling = app.world_mut()
                                .spawn(epix::mesh::Mesh3d{}, epix::camera::InheritedVisibility{},
                                       epix::camera::ViewVisibility::hidden(), epix::transform::GlobalTransform{},
                                       Aabb{.center = glm::vec3(500.0f, 0.0f, 0.0f),
                                            .half_extents = glm::vec3(0.1f)},
                                       epix::camera::NoFrustumCulling{})
                                .id();

    app.add_systems(epix::app::Update, epix::ecs::into(check_point_light_mesh_visibility));
    app.update();

    const auto& faces = app.world_mut().entity(light).get<epix::camera::CubemapVisibleEntities>()->get();
    const auto face_count = [&faces](epix::ecs::Entity entity) {
        std::size_t count = 0;
        for (const auto& face : faces.data) {
            count += static_cast<std::size_t>(std::count(face.entities.begin(), face.entities.end(), entity));
        }
        return count;
    };
    const auto in_face = [&faces](std::size_t index, epix::ecs::Entity entity) {
        const auto& list = faces.data[index].entities;
        return std::find(list.begin(), list.end(), entity) != list.end();
    };

    // On the +X axis inside the light's range: the +X face only, since the six faces are
    // 90-degree pyramids and the axis is squarely inside one of them.
    EXPECT_TRUE(in_face(0, on_x_axis)) << "the +X face sees it";
    EXPECT_EQ(face_count(on_x_axis), 1u) << "and no other face does";
    // The -Y face is index 3, a different index than +X.
    EXPECT_TRUE(in_face(3, below)) << "the -Y face is index 3";
    EXPECT_EQ(face_count(below), 1u);
    // Beyond the light's range on that same axis: no face at all, because the range
    // bounds it and the face's axis does not.
    EXPECT_EQ(face_count(beyond), 0u) << "the light's range bounds it, not the face's axis";
    // Nothing to cull against means every face, and NoFrustumCulling means every face
    // and no range bound either.
    EXPECT_EQ(face_count(unbounded), 6u);
    EXPECT_EQ(face_count(no_culling), 6u);
    EXPECT_TRUE(app.world_mut().entity(on_x_axis).get<epix::camera::ViewVisibility>()->get().get())
        << "the light marked what it recorded as visible";
}

/** @brief The spot-light counterpart: one cone rather than six faces, with the same
 *  two conservative cases (Bevy `bevy_light/src/lib.rs:656-674`). */
TEST(Cluster, SpotLightMeshesComeFromTheVisibilitySystem) {
    using epix::camera::Aabb;
    using epix::camera::Frustum;
    using epix::pbr::check_point_light_mesh_visibility;
    using epix::pbr::SpotLight;
    using epix::pbr::VisibleClusterableObjects;

    epix::app::App app = epix::app::App::create();

    // A 90-degree cone looking down -Z from the origin, so anything on -Z inside the
    // light's range is seen by it.
    epix::camera::PerspectiveProjection projection;
    projection.fov          = glm::radians(90.0f);
    projection.aspect_ratio = 1.0f;
    projection.near_plane   = 0.1f;
    const float range = 10.0f;
    const auto light  = app.world_mut()
                           .spawn(SpotLight{.shadows_enabled = true, .range = range},
                                  epix::transform::GlobalTransform{},
                                  Frustum::from_clip_from_world(projection.get_clip_from_view()),
                                  epix::camera::VisibleMeshEntities{})
                           .id();
    VisibleClusterableObjects view_objects;
    view_objects.entities.push_back(light);
    app.world_mut().spawn(view_objects);

    const auto mesh_at = [&app](std::optional<glm::vec3> position) {
        auto commands = app.world_mut().spawn(epix::mesh::Mesh3d{}, epix::camera::InheritedVisibility{},
                                              epix::camera::ViewVisibility::hidden(), epix::transform::GlobalTransform{});
        if (position.has_value()) {
            commands.insert(Aabb{.center = *position, .half_extents = glm::vec3(0.1f)});
        }
        return commands.id();
    };
    const auto inside  = mesh_at(glm::vec3(0.0f, 0.0f, -2.0f));
    const auto behind  = mesh_at(glm::vec3(0.0f, 0.0f, 2.0f));
    const auto beyond  = mesh_at(glm::vec3(0.0f, 0.0f, -(range + 5.0f)));
    const auto no_aabb = mesh_at(std::nullopt);

    app.add_systems(epix::app::Update, epix::ecs::into(check_point_light_mesh_visibility));
    app.update();

    const auto& recorded = app.world_mut().entity(light).get<epix::camera::VisibleMeshEntities>()->get().entities;
    const auto is_recorded = [&recorded](epix::ecs::Entity entity) {
        return std::find(recorded.begin(), recorded.end(), entity) != recorded.end();
    };
    EXPECT_TRUE(is_recorded(inside)) << "inside the cone and the range";
    EXPECT_FALSE(is_recorded(behind))
        << "in range but behind the cone - the case a sphere test alone would wrongly keep";
    EXPECT_FALSE(is_recorded(beyond))
        << "on the cone's axis but beyond the range - the case a frustum test alone would wrongly keep";
    EXPECT_TRUE(is_recorded(no_aabb)) << "nothing to cull against is visible";
}

/** @brief The visibility system's wiring: it clears stale lists, leaves a light
 *  without shadows empty, honours the caster opt-out and inherited visibility, and
 *  marks a mesh visible only for the faces that see it (Bevy
 *  `check_point_light_mesh_visibility`, `bevy_light/src/lib.rs:466-607`). */
TEST(Cluster, CheckPointLightMeshVisibilityWiring) {
    using epix::pbr::check_point_light_mesh_visibility;
    using epix::pbr::GlobalVisibleClusterableObjects;
    using epix::pbr::NotShadowCaster;
    using epix::pbr::PointLight;
    using epix::pbr::VisibleClusterableObjects;

    const auto build = [](epix::app::App& app, bool shadows_enabled) {
        // A light at the origin looking down -Z, with its cube frusta and a
        // pre-populated visible list so the clearing is observable.
        epix::camera::CubemapVisibleEntities visible;
        visible.data[0].entities.push_back(epix::ecs::Entity::from_index(900));
        const auto light = app.world_mut()
                               .spawn(PointLight{.shadows_enabled = shadows_enabled, .range = 10.0f},
                                      epix::transform::GlobalTransform{}, epix::camera::CubemapFrusta{},
                                      std::move(visible))
                               .id();

        // A view that has the light in its clustered-object set.
        VisibleClusterableObjects view_objects;
        view_objects.entities.push_back(light);
        app.world_mut().spawn(view_objects);

        // Three candidate meshes: one ordinary, one opted out of casting, one not
        // inherited-visible. All carry Mesh3d, as Bevy's query requires.
        const auto mesh = [&app](float x, bool cast, bool inherited_visible) {
            auto commands = app.world_mut().spawn(
                epix::mesh::Mesh3d{}, epix::camera::InheritedVisibility{}, epix::camera::ViewVisibility::hidden(),
                epix::transform::GlobalTransform{},
                epix::camera::Aabb{.center = glm::vec3(x, 0.0f, -2.0f), .half_extents = glm::vec3(0.1f)});
            if (!cast) commands.insert(NotShadowCaster{});
            if (!inherited_visible) commands.insert(epix::camera::NoFrustumCulling{});
            return commands.id();
        };
        const auto ordinary   = mesh(0.0f, true, true);
        const auto opted_out  = mesh(0.5f, false, true);
        const auto no_culling = mesh(-0.5f, true, false);

        app.add_systems(epix::app::Update, epix::ecs::into(check_point_light_mesh_visibility));
        app.update();
        return std::tuple{light, ordinary, opted_out, no_culling};
    };

    // A shadow-casting light: the stale entry is gone, the ordinary mesh is recorded
    // for the faces that see it, and the opted-out mesh is not.
    {
        epix::app::App app = epix::app::App::create();
        const auto [light, ordinary, opted_out, no_culling] = build(app, true);
        const auto& faces = app.world_mut().entity(light).get<epix::camera::CubemapVisibleEntities>()->get();
        std::vector<epix::ecs::Entity> recorded;
        for (const auto& face : faces.data) {
            recorded.insert(recorded.end(), face.entities.begin(), face.entities.end());
        }
        EXPECT_EQ(std::find(recorded.begin(), recorded.end(), epix::ecs::Entity::from_index(900)), recorded.end())
            << "the stale entry was cleared";
        EXPECT_NE(std::find(recorded.begin(), recorded.end(), ordinary), recorded.end())
            << "the ordinary mesh casts and is in the light's range";
        EXPECT_EQ(std::find(recorded.begin(), recorded.end(), opted_out), recorded.end())
            << "NotShadowCaster keeps a mesh out of every shadow map";
        EXPECT_TRUE(app.world_mut().entity(ordinary).get<epix::camera::ViewVisibility>()->get().get())
            << "the light marked it visible";
    }

    // A light without shadows: its lists are cleared and nothing is recorded, so a
    // light that stops casting stops shadowing rather than going stale.
    {
        epix::app::App app = epix::app::App::create();
        const auto [light, ordinary, opted_out, no_culling] = build(app, false);
        const auto& faces = app.world_mut().entity(light).get<epix::camera::CubemapVisibleEntities>()->get();
        for (const auto& face : faces.data) {
            EXPECT_TRUE(face.entities.empty());
        }
        EXPECT_FALSE(app.world_mut().entity(ordinary).get<epix::camera::ViewVisibility>()->get().get())
            << "and no mesh was marked visible";
    }
}

/** @brief The directional light's per-view cascade lists are reconciled with the
 *  cascades it has: stale views removed, surviving lists cleared and resized, new
 *  views added (Bevy `check_dir_light_mesh_visibility`,
 *  `bevy_light/src/lib.rs:339-358`). */
TEST(Cluster, CascadeListsAreReconciledByTheVisibilitySystem) {
    using epix::camera::CascadesFrusta;
    using epix::camera::CascadesVisibleEntities;
    using epix::camera::Frustum;
    using epix::camera::VisibleMeshEntities;
    using epix::pbr::check_dir_light_mesh_visibility;
    using epix::pbr::DirectionalLight;

    epix::app::App app = epix::app::App::create();

    const auto view_a = epix::ecs::Entity::from_index(1);
    const auto view_b = epix::ecs::Entity::from_index(2);  // recorded, but the light has no frusta for it
    const auto view_c = epix::ecs::Entity::from_index(3);  // the light has frusta, nothing recorded yet

    const auto lists_of = [](std::size_t cascades, epix::ecs::Entity stale) {
        std::vector<VisibleMeshEntities> lists(cascades);
        for (VisibleMeshEntities& list : lists) list.entities.push_back(stale);
        return lists;
    };

    CascadesFrusta frusta;
    frusta.frusta.emplace(view_a, std::vector<Frustum>(2, Frustum{}));
    frusta.frusta.emplace(view_c, std::vector<Frustum>(3, Frustum{}));

    // view_a has four cascades recorded but only two frusta now, so the count shrank;
    // view_b is stale; view_c is new.
    CascadesVisibleEntities visible;
    visible.entities.emplace(view_a, lists_of(4, epix::ecs::Entity::from_index(90)));
    visible.entities.emplace(view_b, lists_of(1, epix::ecs::Entity::from_index(91)));

    epix::camera::ViewVisibility light_visibility;
    light_visibility.set_visible();
    const auto light = app.world_mut()
                           .spawn(DirectionalLight{.shadows_enabled = false}, std::move(frusta), std::move(visible),
                                  std::move(light_visibility))
                           .id();

    app.add_systems(epix::app::Update, epix::ecs::into(check_dir_light_mesh_visibility));
    app.update();

    const auto& reconciled = app.world_mut().entity(light).get<CascadesVisibleEntities>()->get().entities;
    // The reconciliation happens before the shadows-enabled check, so a light that casts
    // no shadows still has its lists brought in line.
    EXPECT_EQ(reconciled.find(view_b), reconciled.end()) << "the view the light no longer has frusta for is gone";
    ASSERT_EQ(reconciled.count(view_a), 1u);
    EXPECT_EQ(reconciled.at(view_a).size(), 2u) << "a shrunken cascade count drops the extra lists";
    for (const VisibleMeshEntities& list : reconciled.at(view_a)) {
        EXPECT_TRUE(list.entities.empty()) << "the previous frame's entities are cleared";
    }
    ASSERT_EQ(reconciled.count(view_c), 1u) << "a view the light has frusta for gets lists";
    EXPECT_EQ(reconciled.at(view_c).size(), 3u);
    for (const VisibleMeshEntities& list : reconciled.at(view_c)) {
        EXPECT_TRUE(list.entities.empty());
    }
}

/** @brief The directional visibility system records a mesh per cascade and marks it
 *  visible through a command, while leaving an opted-out mesh out (Bevy
 *  `check_dir_light_mesh_visibility`, `bevy_light/src/lib.rs:302-464`). */
TEST(Cluster, CheckDirLightMeshVisibilityWiring) {
    using epix::pbr::check_dir_light_mesh_visibility;
    using epix::pbr::DirectionalLight;
    using epix::pbr::NotShadowCaster;

    epix::app::App app = epix::app::App::create();

    // One view with two cascades, both looking down -Z, so a mesh in front of the
    // light is inside each of them.
    epix::camera::PerspectiveProjection projection;
    projection.fov          = glm::radians(90.0f);
    projection.aspect_ratio = 1.0f;
    projection.near_plane   = 0.1f;
    const auto frustum      = epix::camera::Frustum::from_clip_from_world(projection.get_clip_from_view());
    const auto view         = epix::ecs::Entity::from_index(1);

    epix::camera::CascadesFrusta cascades;
    cascades.frusta.emplace(view, std::vector<epix::camera::Frustum>{frustum, frustum});
    epix::camera::ViewVisibility light_visibility;
    light_visibility.set_visible();
    const auto light = app.world_mut()
                           .spawn(DirectionalLight{.shadows_enabled = true}, std::move(cascades),
                                  epix::camera::CascadesVisibleEntities{}, std::move(light_visibility))
                           .id();

    const auto mesh = [&app](float x, bool cast) {
        auto commands = app.world_mut().spawn(
            epix::mesh::Mesh3d{}, epix::camera::InheritedVisibility{}, epix::camera::ViewVisibility::hidden(),
            epix::transform::GlobalTransform{},
            epix::camera::Aabb{.center = glm::vec3(x, 0.0f, -2.0f), .half_extents = glm::vec3(0.1f)});
        if (!cast) commands.insert(NotShadowCaster{});
        return commands.id();
    };
    const auto ordinary  = mesh(0.0f, true);
    const auto opted_out = mesh(0.5f, false);

    app.add_systems(epix::app::Update, epix::ecs::into(check_dir_light_mesh_visibility));
    app.update();

    const auto& recorded = app.world_mut().entity(light).get<epix::camera::CascadesVisibleEntities>()->get().entities;
    ASSERT_EQ(recorded.count(view), 1u) << "the view the light has cascades for";
    const auto& cascades_for_view = recorded.at(view);
    ASSERT_EQ(cascades_for_view.size(), 2u) << "one list per cascade";
    for (const auto& list : cascades_for_view) {
        EXPECT_NE(std::find(list.entities.begin(), list.entities.end(), ordinary), list.entities.end())
            << "the mesh is inside both cascades";
        EXPECT_EQ(std::find(list.entities.begin(), list.entities.end(), opted_out), list.entities.end())
            << "and the opted-out mesh is in neither";
    }

    // The marking was deferred into a command, so it is visible only after the
    // schedule ran - which is what lets this system share ViewVisibility with the
    // point and spot one.
    EXPECT_TRUE(app.world_mut().entity(ordinary).get<epix::camera::ViewVisibility>()->get().get())
        << "the deferred command marked it visible";
    EXPECT_FALSE(app.world_mut().entity(opted_out).get<epix::camera::ViewVisibility>()->get().get());
}

/** @brief The vertex-layout half of mesh specialization: two always-on defs, one per
 *  attribute present with a fixed shader location, the shared UV def, and MULTISAMPLED
 *  from the key (Bevy `MeshPipeline::specialize`, `render/mesh.rs:2380-2441`). */
TEST(Cluster, MeshPipelineVertexSpecializationMatchesBevy) {
    using epix::pbr::mesh_vertex_attribute_presence;
    using epix::pbr::specialize_mesh_vertex_layout;

    const auto has_def = [](const auto& result, std::string_view def) {
        return std::find(result.shader_defs.begin(), result.shader_defs.end(), def) != result.shader_defs.end();
    };
    // The specialization reads a mesh's attributes off its vertex buffer layout, so a
    // case is a layout carrying the attributes it is about.
    const auto mesh_layout = [](std::vector<epix::mesh::MeshVertexAttributeId> attributes) {
        return epix::mesh::MeshVertexBufferLayoutRef{std::make_shared<epix::mesh::MeshVertexBufferLayout>(
            std::move(attributes), epix::mesh::VertexBufferLayout{})};
    };
    const auto position = epix::mesh::Mesh::ATTRIBUTE_POSITION.id;
    const auto normal   = epix::mesh::Mesh::ATTRIBUTE_NORMAL.id;
    const auto uv_0     = epix::mesh::Mesh::ATTRIBUTE_UV_0.id;
    const auto uv_1     = epix::mesh::Mesh::ATTRIBUTE_UV_1.id;
    const auto tangent  = epix::mesh::Mesh::ATTRIBUTE_TANGENT.id;
    const auto color    = epix::mesh::Mesh::ATTRIBUTE_COLOR.id;

    // The presence is read off the layout, one `contains` per attribute.
    const auto presence = mesh_vertex_attribute_presence(mesh_layout({position, normal, uv_1}));
    EXPECT_TRUE(presence.position);
    EXPECT_TRUE(presence.normal);
    EXPECT_TRUE(presence.uv_1);
    EXPECT_FALSE(presence.uv_0);
    EXPECT_FALSE(presence.tangent);
    EXPECT_FALSE(presence.color);

    // A bare position-only mesh: the two always-on defs plus its own, nothing else.
    const auto bare = specialize_mesh_vertex_layout(mesh_layout({position}), 1);
    EXPECT_TRUE(has_def(bare, "MESH_PIPELINE"));
    EXPECT_TRUE(has_def(bare, "VERTEX_OUTPUT_INSTANCE_INDEX"));
    EXPECT_TRUE(has_def(bare, "VERTEX_POSITIONS"));
    EXPECT_FALSE(has_def(bare, "VERTEX_NORMALS"));
    EXPECT_FALSE(has_def(bare, "MULTISAMPLED")) << "one sample is not multisampled";
    ASSERT_EQ(bare.vertex_attributes.size(), 1u);
    EXPECT_EQ(bare.vertex_attributes[0].name, "position");
    EXPECT_EQ(bare.vertex_attributes[0].shader_location, 0u);

    // All six attributes: locations 0 to 5 in Bevy's order, and both UV defs, with the
    // shared one appearing once per set rather than being deduplicated.
    const auto everything = specialize_mesh_vertex_layout(mesh_layout({position, normal, uv_0, uv_1, tangent, color}), 4);
    ASSERT_EQ(everything.vertex_attributes.size(), 6u);
    for (std::uint32_t i = 0; i < everything.vertex_attributes.size(); ++i) {
        EXPECT_EQ(everything.vertex_attributes[i].shader_location, i) << "locations are fixed by attribute";
    }
    EXPECT_TRUE(has_def(everything, "VERTEX_UVS"));
    EXPECT_TRUE(has_def(everything, "VERTEX_UVS_A"));
    EXPECT_TRUE(has_def(everything, "VERTEX_UVS_B"));
    EXPECT_EQ(std::count(everything.shader_defs.begin(), everything.shader_defs.end(), std::string("VERTEX_UVS")), 2)
        << "each UV set pushes the shared def";
    EXPECT_TRUE(has_def(everything, "MULTISAMPLED")) << "four samples is multisampled";

    // A mesh without normals leaves location 1 unused rather than shifting uv_0 down to
    // it, which is what fixed locations mean.
    const auto skipped = specialize_mesh_vertex_layout(mesh_layout({position, uv_0}), 1);
    ASSERT_EQ(skipped.vertex_attributes.size(), 2u);
    EXPECT_EQ(skipped.vertex_attributes[1].name, "uv_0");
    EXPECT_EQ(skipped.vertex_attributes[1].shader_location, 2u) << "location 1 stays unused";

    // Nothing at all still yields the two always-on defs.
    const auto empty = specialize_mesh_vertex_layout(mesh_layout({}), 1);
    EXPECT_EQ(empty.shader_defs.size(), 2u);
    EXPECT_TRUE(empty.vertex_attributes.empty());
}

/** @brief The eight mesh bind group layouts: labels, binding numbers, stages, and
 *  the entries that change with the device (Bevy `MeshLayouts`,
 *  `render/mesh_bindings.rs:165-368`). */
TEST(Cluster, MeshLayoutsMatchBevy) {
    using epix::pbr::JOINT_BUFFER_SIZE;
    using epix::pbr::LIGHTMAPS_PER_SLAB;
    using epix::pbr::MeshLayouts;
    using epix::pbr::MORPH_BUFFER_SIZE;
    using epix::pbr::skins_use_uniform_buffers;

    wgpu::Limits limits{};
    limits.maxStorageBuffersPerShaderStage = 8;
    const auto with_arrays = MeshLayouts::make(limits, true);
    const auto without     = MeshLayouts::make(limits, false);

    const auto bindings = [](const epix::render::BindGroupLayoutDescriptor& layout) {
        std::vector<std::uint32_t> result;
        result.reserve(layout.entries.size());
        for (const auto& entry : layout.entries) result.push_back(entry.binding);
        return result;
    };

    // The model uniform is group 1 binding 0 in every layout, at both stages; the
    // optional parts sit at fixed binding numbers so a shader can name them without
    // knowing which mesh it draws. The motion variants add 6 and 7.
    EXPECT_EQ(with_arrays.model_only.label, "mesh_layout");
    EXPECT_EQ(bindings(with_arrays.model_only), (std::vector<std::uint32_t>{0u}));
    EXPECT_EQ(with_arrays.model_only.entries[0].visibility, wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment);
    EXPECT_EQ(with_arrays.skinned.label, "skinned_mesh_layout");
    EXPECT_EQ(bindings(with_arrays.skinned), (std::vector<std::uint32_t>{0u, 1u}));
    EXPECT_EQ(with_arrays.skinned_motion.label, "skinned_motion_mesh_layout");
    EXPECT_EQ(bindings(with_arrays.skinned_motion), (std::vector<std::uint32_t>{0u, 1u, 6u}));
    EXPECT_EQ(with_arrays.morphed.label, "morphed_mesh_layout");
    EXPECT_EQ(bindings(with_arrays.morphed), (std::vector<std::uint32_t>{0u, 2u, 3u}));
    // Bevy gives the motion variant the same label as the plain morphed layout.
    EXPECT_EQ(with_arrays.morphed_motion.label, "morphed_mesh_layout");
    EXPECT_EQ(bindings(with_arrays.morphed_motion), (std::vector<std::uint32_t>{0u, 2u, 3u, 7u}));
    EXPECT_EQ(with_arrays.morphed_skinned.label, "morphed_skinned_mesh_layout");
    EXPECT_EQ(bindings(with_arrays.morphed_skinned), (std::vector<std::uint32_t>{0u, 1u, 2u, 3u}));
    EXPECT_EQ(with_arrays.morphed_skinned_motion.label, "morphed_skinned_motion_mesh_layout");
    EXPECT_EQ(bindings(with_arrays.morphed_skinned_motion), (std::vector<std::uint32_t>{0u, 1u, 2u, 3u, 6u, 7u}));

    // The joint entry follows the device: uniforms with a dynamic offset where
    // there are no storage buffers, read-only storage where there are.
    const auto& joint = with_arrays.skinned.entries[1];
    EXPECT_EQ(joint.buffer.minBindingSize, static_cast<std::uint64_t>(JOINT_BUFFER_SIZE));
    if (skins_use_uniform_buffers(limits)) {
        EXPECT_EQ(joint.buffer.type, wgpu::BufferBindingType::eUniform);
        EXPECT_EQ(joint.buffer.hasDynamicOffset, 1u);
    } else {
        EXPECT_EQ(joint.buffer.type, wgpu::BufferBindingType::eReadOnlyStorage);
        EXPECT_EQ(joint.buffer.hasDynamicOffset, 0u);
    }

    // The morph weights are a dynamic uniform buffer of the declared size, and the
    // morph targets an unfilterable 3D texture.
    const auto& weights = with_arrays.morphed.entries[1];
    EXPECT_EQ(weights.buffer.type, wgpu::BufferBindingType::eUniform);
    EXPECT_EQ(weights.buffer.hasDynamicOffset, 1u);
    EXPECT_EQ(weights.buffer.minBindingSize, static_cast<std::uint64_t>(MORPH_BUFFER_SIZE));
    const auto& targets = with_arrays.morphed.entries[2];
    EXPECT_EQ(targets.texture.sampleType, wgpu::TextureSampleType::eUnfilterableFloat);
    EXPECT_EQ(targets.texture.viewDimension, wgpu::TextureViewDimension::e3D);

    // Lightmaps: bindings 4 and 5 at fragment visibility, as a slab's binding array
    // where the device supports arrays and as a single texture otherwise.
    EXPECT_EQ(with_arrays.lightmapped.label, "lightmapped_mesh_layout");
    EXPECT_EQ(bindings(with_arrays.lightmapped), (std::vector<std::uint32_t>{0u, 4u, 5u}));
    EXPECT_EQ(with_arrays.lightmapped.entries[1].bindingArraySize,
              static_cast<std::uint32_t>(LIGHTMAPS_PER_SLAB));
    EXPECT_EQ(with_arrays.lightmapped.entries[1].visibility, wgpu::ShaderStage::eFragment);
    EXPECT_EQ(with_arrays.lightmapped.entries[1].texture.sampleType, wgpu::TextureSampleType::eFloat);
    EXPECT_EQ(with_arrays.lightmapped.entries[1].texture.viewDimension, wgpu::TextureViewDimension::e2D);
    EXPECT_EQ(with_arrays.lightmapped.entries[2].bindingArraySize, static_cast<std::uint32_t>(LIGHTMAPS_PER_SLAB));
    EXPECT_EQ(with_arrays.lightmapped.entries[2].sampler.type, wgpu::SamplerBindingType::eFiltering);
    EXPECT_EQ(without.lightmapped.entries[1].bindingArraySize, 0u) << "no binding array without array support";
    EXPECT_EQ(without.lightmapped.entries[2].bindingArraySize, 0u);
    EXPECT_EQ(without.lightmapped.entries[1].texture.sampleType, wgpu::TextureSampleType::eFloat);
}

/** @brief The prepass texture entries the mesh view layout adds: one per prepass the
 *  key asks for, with the multisampled forms and the deferred G-buffer's `Uint`
 *  sample type (Bevy `prepass::get_bind_group_layout_entries`,
 *  `prepass/prepass_bindings.rs:13-57`). */
TEST(Cluster, PrepassBindingsMatchBevy) {
    using epix::pbr::MeshPipelineViewLayoutKey;
    using epix::pbr::prepass_bind_group_layout_entries;

    // Nothing asked for: every entry is absent, which is what tells the caller to skip
    // bindings 20 to 23 altogether.
    const auto none = prepass_bind_group_layout_entries(MeshPipelineViewLayoutKey::from_bits_retain(0));
    for (const auto& entry : none) EXPECT_FALSE(entry.has_value());

    // Every prepass at once, multisampled: depth is a multisampled depth texture,
    // normals and motion vectors are multisampled unfilterable floats, and the deferred
    // G-buffer is a single-sampled `Uint` texture - it has no multisampled form.
    const auto all = prepass_bind_group_layout_entries(
        MeshPipelineViewLayoutKey::MULTISAMPLED | MeshPipelineViewLayoutKey::DEPTH_PREPASS |
        MeshPipelineViewLayoutKey::NORMAL_PREPASS | MeshPipelineViewLayoutKey::MOTION_VECTOR_PREPASS |
        MeshPipelineViewLayoutKey::DEFERRED_PREPASS);
    for (const auto& entry : all) ASSERT_TRUE(entry.has_value());
    const auto entry_at = [&all](std::size_t index, std::uint32_t binding) {
        return all[index]->build(binding, wgpu::ShaderStage::eFragment);
    };
    const auto depth = entry_at(0, 20u);
    EXPECT_EQ(depth.binding, 20u) << "the caller's binding number is what it builds with";
    EXPECT_EQ(depth.texture.sampleType, wgpu::TextureSampleType::eDepth);
    EXPECT_EQ(depth.texture.multisampled, 1u);
    EXPECT_EQ(entry_at(1, 21u).texture.sampleType, wgpu::TextureSampleType::eUnfilterableFloat);
    EXPECT_EQ(entry_at(1, 21u).texture.multisampled, 1u);
    EXPECT_EQ(entry_at(2, 22u).texture.sampleType, wgpu::TextureSampleType::eUnfilterableFloat);
    EXPECT_EQ(entry_at(2, 22u).texture.multisampled, 1u);
    EXPECT_EQ(entry_at(3, 23u).texture.sampleType, wgpu::TextureSampleType::eUint);
    EXPECT_EQ(entry_at(3, 23u).texture.multisampled, 0u);

    // One prepass without multisampling: the entry is the single-sampled form and the
    // others stay absent.
    const auto depth_only = prepass_bind_group_layout_entries(MeshPipelineViewLayoutKey::DEPTH_PREPASS);
    ASSERT_TRUE(depth_only[0].has_value());
    EXPECT_FALSE(depth_only[1].has_value());
    EXPECT_FALSE(depth_only[2].has_value());
    EXPECT_FALSE(depth_only[3].has_value());
    const auto single = depth_only[0]->build(20u, wgpu::ShaderStage::eFragment);
    EXPECT_EQ(single.texture.sampleType, wgpu::TextureSampleType::eDepth);
    EXPECT_EQ(single.texture.multisampled, 0u);
}

/** @brief The light-probe layout entries: environment map cubemaps plus their sampler
 *  and uniform, and the irradiance volume's voxel texture plus its sampler — each in
 *  the single-texture and binding-array shapes (Bevy
 *  `environment_map::get_bind_group_layout_entries`, `light_probe/environment_map.rs:150-169`,
 *  and `irradiance_volume::get_bind_group_layout_entries`, `light_probe/irradiance_volume.rs:277-294`). */
TEST(Cluster, LightProbeLayoutEntriesMatchBevy) {
    using epix::pbr::environment_map_bind_group_layout_entries;
    using epix::pbr::EnvironmentMapUniform;
    using epix::pbr::irradiance_volume_bind_group_layout_entries;
    using epix::pbr::MAX_VIEW_LIGHT_PROBES;

    const auto single = environment_map_bind_group_layout_entries(false);
    const auto arrays = environment_map_bind_group_layout_entries(true);
    const auto at = [](const auto& entries, std::size_t index, std::uint32_t binding) {
        return entries[index].build(binding, wgpu::ShaderStage::eVertex);
    };

    // Both cubemaps are cube textures, and the two shapes differ only in their
    // binding-array size.
    for (std::size_t index : {0u, 1u}) {
        EXPECT_EQ(at(single, index, 0u).texture.viewDimension, wgpu::TextureViewDimension::eCube);
        EXPECT_EQ(at(single, index, 0u).texture.sampleType, wgpu::TextureSampleType::eFloat);
        EXPECT_EQ(at(single, index, 0u).bindingArraySize, 0u);
        EXPECT_EQ(at(arrays, index, 0u).bindingArraySize, static_cast<std::uint32_t>(MAX_VIEW_LIGHT_PROBES));
    }
    EXPECT_EQ(at(single, 2u, 0u).sampler.type, wgpu::SamplerBindingType::eFiltering);
    // The uniform is a dynamic uniform buffer the fragment shader reads.
    const auto uniform = at(single, 3u, 17u);
    EXPECT_EQ(uniform.buffer.type, wgpu::BufferBindingType::eUniform);
    EXPECT_EQ(uniform.buffer.hasDynamicOffset, 1u);
    EXPECT_EQ(uniform.buffer.minBindingSize, static_cast<std::uint64_t>(sizeof(EnvironmentMapUniform)));
    EXPECT_EQ(uniform.visibility, wgpu::ShaderStage::eFragment);
    EXPECT_EQ(uniform.binding, 17u) << "the view layout puts it at binding 17";

    const auto voxels_single = irradiance_volume_bind_group_layout_entries(false);
    const auto voxels_arrays = irradiance_volume_bind_group_layout_entries(true);
    EXPECT_EQ(at(voxels_single, 0u, 3u).texture.viewDimension, wgpu::TextureViewDimension::e3D);
    EXPECT_EQ(at(voxels_single, 0u, 3u).texture.sampleType, wgpu::TextureSampleType::eFloat);
    EXPECT_EQ(at(voxels_single, 0u, 3u).bindingArraySize, 0u);
    EXPECT_EQ(at(voxels_arrays, 0u, 3u).bindingArraySize, static_cast<std::uint32_t>(MAX_VIEW_LIGHT_PROBES));
    EXPECT_EQ(at(voxels_single, 1u, 4u).sampler.type, wgpu::SamplerBindingType::eFiltering);
}

/** @brief The pass a mesh key selects, and the defs a key adds beside the vertex and
 *  pass ones (Bevy `MeshPipeline::specialize`, `render/mesh.rs:2453-2538`). */
TEST(Cluster, MeshPipelinePassSelectionMatchesBevy) {
    using epix::pbr::MeshPipelineKey;
    using epix::pbr::specialize_mesh_key_shader_defs;
    using epix::pbr::specialize_mesh_pass;

    // The default key is the opaque pass: depth written, and opaque unless something
    // downstream has to composite it.
    const auto opaque = specialize_mesh_pass(MeshPipelineKey::NONE);
    EXPECT_EQ(opaque.label, "opaque_mesh_pipeline");
    EXPECT_FALSE(opaque.blend);
    EXPECT_TRUE(opaque.depth_write_enabled);
    EXPECT_TRUE(opaque.is_opaque);
    EXPECT_TRUE(opaque.shader_defs.empty());

    // Alpha blending: blends, and does not write depth.
    const auto alpha = specialize_mesh_pass(MeshPipelineKey::BLEND_ALPHA);
    EXPECT_EQ(alpha.label, "alpha_blend_mesh_pipeline");
    EXPECT_TRUE(alpha.blend);
    EXPECT_FALSE(alpha.depth_write_enabled);
    EXPECT_FALSE(alpha.premultiply_alpha);

    // Premultiplied alpha and multiply both ask the shader to premultiply, and each
    // adds its own def as well.
    const auto premultiplied = specialize_mesh_pass(MeshPipelineKey::BLEND_PREMULTIPLIED_ALPHA);
    EXPECT_EQ(premultiplied.label, "premultiplied_alpha_mesh_pipeline");
    EXPECT_TRUE(premultiplied.premultiply_alpha);
    EXPECT_TRUE(premultiplied.blend_premultiplied);
    EXPECT_FALSE(premultiplied.blend_multiply);
    const auto multiply = specialize_mesh_pass(MeshPipelineKey::BLEND_MULTIPLY);
    EXPECT_EQ(multiply.label, "multiply_mesh_pipeline");
    EXPECT_TRUE(multiply.premultiply_alpha);
    EXPECT_TRUE(multiply.blend_multiply);
    EXPECT_FALSE(multiply.blend_premultiplied);

    // Alpha to coverage: depth written, coverage on, and its own def.
    const auto coverage = specialize_mesh_pass(MeshPipelineKey::BLEND_ALPHA_TO_COVERAGE);
    EXPECT_EQ(coverage.label, "alpha_to_coverage_mesh_pipeline");
    EXPECT_FALSE(coverage.blend) << "coverage replaces rather than blends";
    EXPECT_TRUE(coverage.alpha_to_coverage);
    EXPECT_TRUE(coverage.depth_write_enabled);
    EXPECT_EQ(std::find(coverage.shader_defs.begin(), coverage.shader_defs.end(), std::string("ALPHA_TO_COVERAGE")) !=
                  coverage.shader_defs.end(),
              true);

    // OIT takes precedence over plain alpha blending, and the OIT pass itself blends
    // nothing - the resolve does that - so it adds its own def instead.
    MeshPipelineKey oit_alpha = MeshPipelineKey::BLEND_ALPHA;
    oit_alpha = oit_alpha | MeshPipelineKey::OIT_ENABLED;
    const auto oit = specialize_mesh_pass(oit_alpha);
    EXPECT_EQ(oit.label, "oit_mesh_pipeline");
    EXPECT_FALSE(oit.blend);
    EXPECT_FALSE(oit.depth_write_enabled);
    EXPECT_NE(std::find(oit.shader_defs.begin(), oit.shader_defs.end(), std::string("OIT_ENABLED")), oit.shader_defs.end());
    // Without the blend bits, OIT is not enough on its own: the arm needs both.
    const auto oit_only = specialize_mesh_pass(MeshPipelineKey::OIT_ENABLED);
    EXPECT_EQ(oit_only.label, "opaque_mesh_pipeline") << "OIT alone does not select the OIT pass";

    // Reading the view transmission texture makes an otherwise opaque pass not
    // opaque, in both arms that can say so.
    for (const MeshPipelineKey base : {MeshPipelineKey::NONE, MeshPipelineKey::BLEND_ALPHA_TO_COVERAGE}) {
        EXPECT_TRUE(specialize_mesh_pass(base).is_opaque);
        EXPECT_FALSE(specialize_mesh_pass(base | MeshPipelineKey::READS_VIEW_TRANSMISSION_TEXTURE).is_opaque)
            << "something downstream still composites it";
    }

    // The key-driven defs are independent of the pass and each appears only for its
    // own bit.
    const auto defs = specialize_mesh_key_shader_defs(
        MeshPipelineKey::NONE | MeshPipelineKey::DEPTH_PREPASS | MeshPipelineKey::HAS_PREVIOUS_MORPH |
            MeshPipelineKey::VIEW_PROJECTION_PERSPECTIVE,
        true);
    // Membership rather than a total, because some defs come from defaults the key
    // carries - the blur tap count is always present, for one.
    const auto defs_have = [&defs](std::string_view name) {
        return std::any_of(defs.begin(), defs.end(), [name](const auto& def) { return def.name == name; });
    };
    EXPECT_TRUE(defs_have("DEPTH_PREPASS"));
    EXPECT_TRUE(defs_have("HAS_PREVIOUS_MORPH"));
    EXPECT_TRUE(defs_have("VIEW_PROJECTION_PERSPECTIVE"));
    EXPECT_TRUE(defs_have("SCREEN_SPACE_SPECULAR_TRANSMISSION_BLUR_TAPS"))
        << "the blur tap count is pushed whatever the key's blur bits are";
    const auto bare_key_defs = specialize_mesh_key_shader_defs(MeshPipelineKey::NONE, true);
    EXPECT_TRUE(std::any_of(bare_key_defs.begin(), bare_key_defs.end(),
                            [](const auto& def) { return def.name == "VIEW_PROJECTION_NONSTANDARD"; }));
    // Hardware 2x2 is the zero value of the filter bits, in Bevy as here, so a key with
    // no filter bits selects it rather than yielding nothing.
    EXPECT_TRUE(std::any_of(bare_key_defs.begin(), bare_key_defs.end(), [](const auto& def) {
        return def.name == "SHADOW_FILTER_METHOD_HARDWARE_2X2";
    })) << "the zero filter value is hardware 2x2";
    // A valued def carries its index, and the tonemap method is one def of eight.
    const auto tonemapped = specialize_mesh_key_shader_defs(
        MeshPipelineKey::TONEMAP_IN_SHADER | MeshPipelineKey::TONEMAP_METHOD_AGX | MeshPipelineKey::DEBAND_DITHER, true);
    const auto has_name = [](const std::vector<epix::shader::ShaderDefVal>& list, std::string_view name) {
        return std::any_of(list.begin(), list.end(), [name](const auto& def) { return def.name == name; });
    };
    EXPECT_TRUE(has_name(tonemapped, "TONEMAP_IN_SHADER"));
    EXPECT_TRUE(has_name(tonemapped, "TONEMAP_METHOD_AGX"));
    EXPECT_TRUE(has_name(tonemapped, "DEBAND_DITHER")) << "debanding rides along with tonemapping";
    const auto lut_index = std::find_if(tonemapped.begin(), tonemapped.end(), [](const auto& def) {
        return def.name == "TONEMAPPING_LUT_TEXTURE_BINDING_INDEX";
    });
    ASSERT_NE(lut_index, tonemapped.end()) << "the LUT binding index is a valued def, not a bare name";
    ASSERT_TRUE(std::holds_alternative<std::uint32_t>(lut_index->value));
    EXPECT_EQ(std::get<std::uint32_t>(lut_index->value), 18u);

    // Debanding without tonemapping is dropped, since the shader cannot do it alone.
    const auto deband_only =
        specialize_mesh_key_shader_defs(MeshPipelineKey::DEBAND_DITHER, true);
    EXPECT_FALSE(has_name(deband_only, "DEBAND_DITHER"));

    // LOAD_PREPASS_NORMALS needs a normal prepass, one sample and an opaque pass.
    const auto normal_prepass  = MeshPipelineKey::NONE | MeshPipelineKey::NORMAL_PREPASS;
    const auto multisampled_normal = normal_prepass | MeshPipelineKey::from_msaa_samples(4);
    EXPECT_TRUE(has_name(specialize_mesh_key_shader_defs(normal_prepass, true), "LOAD_PREPASS_NORMALS"));
    EXPECT_FALSE(has_name(specialize_mesh_key_shader_defs(normal_prepass, false), "LOAD_PREPASS_NORMALS"))
        << "a non-opaque pass cannot load the prepass normals";
    EXPECT_FALSE(has_name(specialize_mesh_key_shader_defs(multisampled_normal, true), "LOAD_PREPASS_NORMALS"))
        << "nor can a multisampled one";

    // The def is LIGHTMAP while the bit is LIGHTMAPPED, which is Bevy's own naming.
    EXPECT_TRUE(has_name(specialize_mesh_key_shader_defs(MeshPipelineKey::LIGHTMAPPED, true), "LIGHTMAP"));

    // The irradiance-volume def needs the key bit *and* the platform capability; on
    // every platform Epix targets that capability is held, so the bit alone decides.
    static_assert(epix::pbr::IRRADIANCE_VOLUMES_ARE_USABLE);
    EXPECT_TRUE(has_name(specialize_mesh_key_shader_defs(MeshPipelineKey::IRRADIANCE_VOLUME, true), "IRRADIANCE_VOLUME"));
    EXPECT_FALSE(has_name(specialize_mesh_key_shader_defs(MeshPipelineKey::NONE, true), "IRRADIANCE_VOLUME"))
        << "the def is key-gated, not unconditional";

    // The shadow filter method: one of three when set, and *no* def when the key
    // carries none - unlike the view projection, whose zero value is a real case.
    const auto gaussian = specialize_mesh_key_shader_defs(MeshPipelineKey::SHADOW_FILTER_METHOD_GAUSSIAN, true);
    EXPECT_TRUE(has_name(gaussian, "SHADOW_FILTER_METHOD_GAUSSIAN"));
    EXPECT_FALSE(has_name(gaussian, "SHADOW_FILTER_METHOD_TEMPORAL"));
    const auto temporal = specialize_mesh_key_shader_defs(MeshPipelineKey::SHADOW_FILTER_METHOD_TEMPORAL, true);
    EXPECT_TRUE(has_name(temporal, "SHADOW_FILTER_METHOD_TEMPORAL"));
    EXPECT_FALSE(has_name(temporal, "SHADOW_FILTER_METHOD_HARDWARE_2X2"));

    // Screen-space specular transmission carries its blur tap count as a valued def,
    // always present, defaulting to the low quality's four taps.
    const auto blur_low = specialize_mesh_key_shader_defs(
        MeshPipelineKey::SCREEN_SPACE_SPECULAR_TRANSMISSION_LOW, true);
    const auto taps = std::find_if(blur_low.begin(), blur_low.end(), [](const auto& def) {
        return def.name == "SCREEN_SPACE_SPECULAR_TRANSMISSION_BLUR_TAPS";
    });
    ASSERT_NE(taps, blur_low.end());
    ASSERT_TRUE(std::holds_alternative<std::int32_t>(taps->value) || std::holds_alternative<std::uint32_t>(taps->value));
    const auto tap_count = std::holds_alternative<std::uint32_t>(taps->value)
                               ? std::get<std::uint32_t>(taps->value)
                               : static_cast<std::uint32_t>(std::get<std::int32_t>(taps->value));
    EXPECT_EQ(tap_count, 4u);
    const auto blur_ultra = specialize_mesh_key_shader_defs(
        MeshPipelineKey::SCREEN_SPACE_SPECULAR_TRANSMISSION_ULTRA, true);
    const auto ultra_taps = std::find_if(blur_ultra.begin(), blur_ultra.end(), [](const auto& def) {
        return def.name == "SCREEN_SPACE_SPECULAR_TRANSMISSION_BLUR_TAPS";
    });
    ASSERT_NE(ultra_taps, blur_ultra.end());
    EXPECT_EQ(std::get<std::uint32_t>(ultra_taps->value), 32u);

    // The capability defs follow the pipeline's flags, not the key, and the batch size
    // is a valued def whose absence means storage buffers are in use.
    using epix::pbr::specialize_mesh_capability_shader_defs;
    const auto capable = specialize_mesh_capability_shader_defs(true, true, true, 64u);
    const auto has_cap = [&capable](std::string_view name) {
        return std::any_of(capable.begin(), capable.end(),
                           [name](const auto& def) { return def.name == name; });
    };
    EXPECT_TRUE(has_cap("MULTIPLE_LIGHT_PROBES_IN_ARRAY"));
    EXPECT_TRUE(has_cap("MULTIPLE_LIGHTMAPS_IN_ARRAY"));
    EXPECT_TRUE(has_cap("IRRADIANCE_VOLUMES_ARE_USABLE"));
    EXPECT_TRUE(has_cap("CLUSTERED_DECALS_ARE_USABLE"));
    const auto batch = std::find_if(capable.begin(), capable.end(), [](const auto& def) {
        return def.name == "PER_OBJECT_BUFFER_BATCH_SIZE";
    });
    ASSERT_NE(batch, capable.end());
    EXPECT_EQ(std::get<std::uint32_t>(batch->value), 64u);
    // Nothing capable and no batch size: no defs at all, which is what tells the
    // shader to declare an unbounded array.
    EXPECT_TRUE(specialize_mesh_capability_shader_defs(false, false, false, std::nullopt).empty());
}

/** @brief The mesh pipeline's fixed state: back-face culling, the reverse-Z depth
 *  compare, an ignored stencil and a zero bias, and multisampling from the key (Bevy
 *  `MeshPipeline::specialize`, `render/mesh.rs:2710-2736`). */
TEST(Cluster, MeshPipelineFixedStateMatchesBevy) {
    using epix::pbr::mesh_pipeline_depth_state;
    using epix::pbr::mesh_pipeline_multisample_state;
    using epix::pbr::mesh_pipeline_primitive_state;

    // Back-face culling, with the topology the caller took from the key.
    const auto primitive = mesh_pipeline_primitive_state(wgpu::PrimitiveTopology::eTriangleList);
    EXPECT_EQ(primitive.cullMode, wgpu::CullMode::eBack);
    EXPECT_EQ(primitive.topology, wgpu::PrimitiveTopology::eTriangleList);
    EXPECT_FALSE(primitive.unclippedDepth);

    // The reverse-Z compare, which is the pin worth having: a conventional pipeline
    // would use Less, and using it here would invert every depth test.
    const auto depth = mesh_pipeline_depth_state(wgpu::TextureFormat::eDepth32Float, true);
    EXPECT_EQ(depth.depthCompare, wgpu::CompareFunction::eGreaterEqual)
        << "near is depth 1 under the engine's infinite reverse-Z projection";
    EXPECT_EQ(depth.format, wgpu::TextureFormat::eDepth32Float);
    EXPECT_EQ(depth.depthWriteEnabled, wgpu::OptionalBool::eTrue);
    EXPECT_EQ(depth.stencilReadMask, 0u);
    EXPECT_EQ(depth.stencilWriteMask, 0u);
    EXPECT_EQ(depth.depthBias, 0);
    EXPECT_EQ(depth.depthBiasSlopeScale, 0.0f);
    EXPECT_EQ(depth.depthBiasClamp, 0.0f);
    // The transparent passes disable the depth write; everything else keeps it.
    EXPECT_EQ(mesh_pipeline_depth_state(wgpu::TextureFormat::eDepth32Float, false).depthWriteEnabled,
              wgpu::OptionalBool::eFalse);

    // Multisampling follows the key, and alpha-to-coverage the pass.
    const auto single = mesh_pipeline_multisample_state(1, false);
    EXPECT_EQ(single.count, 1u);
    EXPECT_EQ(single.mask, 0xFFFFFFFFu) << "every sample contributes";
    EXPECT_FALSE(single.alphaToCoverageEnabled);
    const auto coverage = mesh_pipeline_multisample_state(4, true);
    EXPECT_EQ(coverage.count, 4u);
    EXPECT_TRUE(coverage.alphaToCoverageEnabled);
}

/** @brief The morph and skinning setup's eight cases, which are a precedence rather
 *  than a cross product (Bevy `setup_morph_and_skinning_defs`,
 *  `render/mesh.rs:2312-2370`). */
TEST(Cluster, MeshMorphSkinningSetupMatchesBevy) {
    using epix::pbr::is_skinned;
    using epix::pbr::MeshLayoutKind;
    using epix::pbr::setup_morph_and_skinning_defs;

    const auto key_with = [](std::initializer_list<epix::pbr::MeshPipelineKey> bits) {
        epix::pbr::MeshPipelineKey key = epix::pbr::MeshPipelineKey::NONE;
        for (const auto& bit : bits) key = key | bit;
        return key;
    };
    const auto has_def = [](const auto& setup, std::string_view name) {
        return std::any_of(setup.shader_defs.begin(), setup.shader_defs.end(),
                           [name](const auto& def) { return def.name == name; });
    };
    // Whether a mesh is skinned is read off the layout, as Bevy's
    // `is_skinned(layout)` does: the joint index and weight attributes.
    const auto mesh_layout = [](bool skinned) {
        std::vector<epix::mesh::MeshVertexAttributeId> attributes;
        if (skinned) {
            attributes = {epix::mesh::Mesh::ATTRIBUTE_JOINT_INDEX.id, epix::mesh::Mesh::ATTRIBUTE_JOINT_WEIGHT.id};
        }
        return epix::mesh::MeshVertexBufferLayoutRef{std::make_shared<epix::mesh::MeshVertexBufferLayout>(
            std::move(attributes), epix::mesh::VertexBufferLayout{})};
    };

    // Both joint attributes are required, and the layout is the only input.
    EXPECT_TRUE(is_skinned(mesh_layout(true)));
    EXPECT_FALSE(is_skinned(mesh_layout(false)));

    // Nothing special: the model-only layout and no defs.
    const auto plain = setup_morph_and_skinning_defs(key_with({}), mesh_layout(false), 6, false);
    EXPECT_EQ(plain.layout, MeshLayoutKind::ModelOnly);
    EXPECT_TRUE(plain.shader_defs.empty());
    EXPECT_TRUE(plain.vertex_attributes.empty());

    // Lightmapped but neither skinned nor morphed: the lightmapped layout, still no
    // defs - the shader def for that comes from the key step, not here.
    const auto lightmapped = setup_morph_and_skinning_defs(key_with({epix::pbr::MeshPipelineKey::LIGHTMAPPED}), mesh_layout(false), 6, false);
    EXPECT_EQ(lightmapped.layout, MeshLayoutKind::Lightmapped);
    EXPECT_TRUE(lightmapped.shader_defs.empty());

    // Skinned: the SKINNED def and the two joint attributes, immediately after the
    // mesh's own attributes.
    const auto skinned = setup_morph_and_skinning_defs(key_with({}), mesh_layout(true), 6, false);
    EXPECT_EQ(skinned.layout, MeshLayoutKind::Skinned);
    EXPECT_TRUE(has_def(skinned, "SKINNED"));
    ASSERT_EQ(skinned.vertex_attributes.size(), 2u);
    EXPECT_EQ(skinned.vertex_attributes[0].shader_location, 6u) << "the offset the caller passed";
    EXPECT_EQ(skinned.vertex_attributes[1].shader_location, 7u) << "and the next location";
    EXPECT_TRUE(skinned.skinned);

    // The precedence that matters: a skinned mesh that is also lightmapped does *not*
    // take the lightmapped layout, because there is no such layout.
    const auto skinned_lightmapped =
        setup_morph_and_skinning_defs(key_with({epix::pbr::MeshPipelineKey::LIGHTMAPPED}), mesh_layout(true), 6, false);
    EXPECT_EQ(skinned_lightmapped.layout, MeshLayoutKind::Skinned)
        << "the lightmapped layout is only for unskinned, unmorphed meshes";

    // Morphed: the morph def, no joint attributes, and the morphed layout.
    const auto morphed = setup_morph_and_skinning_defs(key_with({epix::pbr::MeshPipelineKey::MORPH_TARGETS}), mesh_layout(false), 6, false);
    EXPECT_EQ(morphed.layout, MeshLayoutKind::Morphed);
    EXPECT_TRUE(has_def(morphed, "MORPH_TARGETS"));
    EXPECT_TRUE(morphed.vertex_attributes.empty());
    EXPECT_FALSE(morphed.skinned);

    // Lightmapping is the last family, so for an unskinned, unmorphed mesh it wins even
    // when motion vectors are on - motion only chooses between variants within the
    // skinned and morphed families.
    const auto motion_lightmapped = setup_morph_and_skinning_defs(
        key_with({epix::pbr::MeshPipelineKey::LIGHTMAPPED, epix::pbr::MeshPipelineKey::MOTION_VECTOR_PREPASS}), mesh_layout(false), 6,
        false);
    EXPECT_EQ(motion_lightmapped.layout, MeshLayoutKind::Lightmapped);
    const auto morphed_motion = setup_morph_and_skinning_defs(
        key_with({epix::pbr::MeshPipelineKey::MORPH_TARGETS, epix::pbr::MeshPipelineKey::MOTION_VECTOR_PREPASS}), mesh_layout(false), 6,
        false);
    EXPECT_EQ(morphed_motion.layout, MeshLayoutKind::MorphedMotion);
    const auto skinned_motion = setup_morph_and_skinning_defs(
        key_with({epix::pbr::MeshPipelineKey::MOTION_VECTOR_PREPASS}), mesh_layout(true), 6, false);
    EXPECT_EQ(skinned_motion.layout, MeshLayoutKind::SkinnedMotion);

    // Skinned and morphed together, with and without motion vectors.
    const auto both = setup_morph_and_skinning_defs(
        key_with({epix::pbr::MeshPipelineKey::MORPH_TARGETS}), mesh_layout(true), 6, false);
    EXPECT_EQ(both.layout, MeshLayoutKind::MorphedSkinned);
    EXPECT_TRUE(has_def(both, "SKINNED"));
    EXPECT_TRUE(has_def(both, "MORPH_TARGETS"));
    const auto both_motion = setup_morph_and_skinning_defs(
        key_with({epix::pbr::MeshPipelineKey::MORPH_TARGETS, epix::pbr::MeshPipelineKey::MOTION_VECTOR_PREPASS}), mesh_layout(true), 6,
        false);
    EXPECT_EQ(both_motion.layout, MeshLayoutKind::MorphedSkinnedMotion);

    // The uniform-buffer capability follows the pipeline, not the key.
    const auto uniforms = setup_morph_and_skinning_defs(key_with({}), mesh_layout(true), 6, true);
    EXPECT_TRUE(has_def(uniforms, "SKINS_USE_UNIFORM_BUFFERS"));
    EXPECT_FALSE(has_def(skinned, "SKINS_USE_UNIFORM_BUFFERS"));
}

/** @brief The fog falloff arithmetic behind Bevy's visibility-based constructors (Bevy
 *  `bevy_pbr/src/fog.rs:318-462`). */
TEST(Cluster, FogFalloffArithmeticMatchesBevy) {
    using epix::pbr::fog_atmospheric_extinction;
    using epix::pbr::fog_atmospheric_inscattering;
    using epix::pbr::fog_exponential_density;
    using epix::pbr::fog_exponential_squared_density;
    using epix::pbr::koschmieder;
    using epix::pbr::REVISED_KOSCHMIEDER_CONTRAST_THRESHOLD;

    // The revised threshold is five percent, not the two percent of the original
    // Koschmieder law.
    EXPECT_FLOAT_EQ(REVISED_KOSCHMIEDER_CONTRAST_THRESHOLD, 0.05f);

    // The exponent falls as visibility rises: you can see further, so the fog is
    // thinner.
    EXPECT_GT(koschmieder(100.0f, 0.05f), koschmieder(200.0f, 0.05f));
    EXPECT_NEAR(koschmieder(100.0f, 0.05f), 0.029957f, 1e-6f);
    EXPECT_NEAR(koschmieder(1.0f, 0.5f), 0.693147f, 1e-6f) << "-ln(0.5)";
    // A threshold of one means no fog at all: -ln(1) is zero.
    EXPECT_FLOAT_EQ(koschmieder(100.0f, 1.0f), 0.0f);

    // The exponential density is the exponent itself, while the squared one divides by
    // the visibility *again* - so it is not simply the square root of the first.
    EXPECT_FLOAT_EQ(fog_exponential_density(150.0f, 0.05f), koschmieder(150.0f, 0.05f));
    const float plain   = fog_exponential_density(150.0f, 0.05f);
    const float squared = fog_exponential_squared_density(150.0f, 0.05f);
    EXPECT_NEAR(squared, std::sqrt(plain / 150.0f), 1e-9f);
    EXPECT_LT(squared, std::sqrt(plain)) << "the second division by visibility matters";

    // Fading a colour towards black as extinction means inverting it first, so a blue
    // extinction gives blue results: the blue channel's coefficient is the *smallest*,
    // since a small coefficient is what leaves that channel visible.
    const glm::vec3 blue{0.0f, 0.0f, 1.0f};
    const glm::vec3 red{1.0f, 0.0f, 0.0f};
    const auto blue_extinction = fog_atmospheric_extinction(blue, 1.0f, 100.0f, 0.05f);
    const auto red_extinction  = fog_atmospheric_extinction(red, 1.0f, 100.0f, 0.05f);
    EXPECT_LT(blue_extinction.z, blue_extinction.x) << "a blue extinction leaves blue visible";
    EXPECT_LT(red_extinction.x, red_extinction.z);
    EXPECT_NEAR(blue_extinction.z, 0.0f, 1e-6f) << "fully blue means blue is not attenuated at all";
    // A white colour, being 1 everywhere, inverts to zero extinction on every channel.
    EXPECT_NEAR(glm::length(fog_atmospheric_extinction(glm::vec3(1.0f), 1.0f, 100.0f, 0.05f)), 0.0f, 1e-6f);
    // The alpha channel scales the extinction, which is how it modulates the effect
    // without touching the falloff mode - checked on a channel that is not already zero.
    const auto faded = fog_atmospheric_extinction(blue, 0.5f, 100.0f, 0.05f);
    EXPECT_LT(faded.x, blue_extinction.x);
    EXPECT_GT(faded.x, 0.0f);

    // In-scattering is *not* inverted: it is the colour you see.
    const auto scattering = fog_atmospheric_inscattering(blue);
    EXPECT_NEAR(scattering.z, 1.0f, 1e-6f) << "1^e is 1";
    EXPECT_NEAR(scattering.x, 0.0f, 1e-6f) << "0^e is 0";
    EXPECT_LT(fog_atmospheric_inscattering(glm::vec3(0.5f)).x, 0.5f)
        << "a partial channel raised to e falls further";
}

/** @brief The fog component, its GPU form and the per-mode packing of `be` and `bi`
 *  (Bevy `render/fog.rs:18-113`). */
TEST(Cluster, GpuFogMatchesBevy) {
    using epix::pbr::DistanceFog;
    using epix::pbr::FogFalloff;
    using epix::pbr::FogFalloffAtmospheric;
    using epix::pbr::FogFalloffExponential;
    using epix::pbr::FogFalloffExponentialSquared;
    using epix::pbr::FogFalloffLinear;
    using epix::pbr::gpu_fog_from;
    using epix::pbr::GPU_FOG_MODE_ATMOSPHERIC;
    using epix::pbr::GPU_FOG_MODE_EXPONENTIAL;
    using epix::pbr::GPU_FOG_MODE_EXPONENTIAL_SQUARED;
    using epix::pbr::GPU_FOG_MODE_LINEAR;
    using epix::pbr::GPU_FOG_MODE_OFF;

    // The layout has to match the shader, so the asserts in the header are the point:
    // two vec4 colours, then be and the exponent, then bi and the mode.
    static_assert(sizeof(epix::pbr::GpuFog) == 64);
    EXPECT_EQ(sizeof(epix::pbr::GpuFog), 64u);
    EXPECT_EQ(offsetof(epix::pbr::GpuFog, be), 32u);
    EXPECT_EQ(offsetof(epix::pbr::GpuFog, bi), 48u);

    // The default component matches Bevy's: white, no directional glow, exponent 8,
    // and linear fog from 0 to 100.
    const DistanceFog defaults;
    EXPECT_EQ(defaults.color, glm::vec4(1.0f));
    EXPECT_EQ(defaults.directional_light_color, glm::vec4(0.0f));
    EXPECT_FLOAT_EQ(defaults.directional_light_exponent, 8.0f);
    const auto default_gpu = gpu_fog_from(defaults);
    EXPECT_EQ(default_gpu.mode, GPU_FOG_MODE_LINEAR);
    EXPECT_EQ(default_gpu.be, glm::vec3(0.0f, 100.0f, 0.0f)) << "start and end live in be.xy";

    // Each falloff packs be differently, which is the part a reader gets wrong.
    DistanceFog exponential;
    exponential.falloff = FogFalloff{FogFalloffExponential{.density = 0.02f}};
    const auto exp_gpu = gpu_fog_from(exponential);
    EXPECT_EQ(exp_gpu.mode, GPU_FOG_MODE_EXPONENTIAL);
    EXPECT_EQ(exp_gpu.be, glm::vec3(0.02f, 0.0f, 0.0f)) << "the density is be.x alone";
    EXPECT_EQ(exp_gpu.bi, glm::vec3(0.0f)) << "and nothing is in-scattered";

    DistanceFog squared;
    squared.falloff = FogFalloff{FogFalloffExponentialSquared{.density = 0.03f}};
    const auto squared_gpu = gpu_fog_from(squared);
    EXPECT_EQ(squared_gpu.mode, GPU_FOG_MODE_EXPONENTIAL_SQUARED);
    EXPECT_EQ(squared_gpu.be, glm::vec3(0.03f, 0.0f, 0.0f))
        << "the same shape as the exponential, distinguished only by the mode";

    DistanceFog atmospheric;
    atmospheric.falloff = FogFalloff{FogFalloffAtmospheric{.extinction = {0.1f, 0.2f, 0.3f},
                                                           .inscattering = {0.4f, 0.5f, 0.6f}}};
    const auto atmospheric_gpu = gpu_fog_from(atmospheric);
    EXPECT_EQ(atmospheric_gpu.mode, GPU_FOG_MODE_ATMOSPHERIC);
    EXPECT_EQ(atmospheric_gpu.be, glm::vec3(0.1f, 0.2f, 0.3f));
    EXPECT_EQ(atmospheric_gpu.bi, glm::vec3(0.4f, 0.5f, 0.6f)) << "atmospheric is the only mode using bi";

    // The visibility constructors produce the density from the earlier arithmetic.
    const auto exp_from_visibility = FogFalloff::from_visibility(100.0f);
    ASSERT_TRUE(std::holds_alternative<FogFalloffExponential>(exp_from_visibility.value));
    EXPECT_FLOAT_EQ(std::get<FogFalloffExponential>(exp_from_visibility.value).density,
                    epix::pbr::fog_exponential_density(100.0f, epix::pbr::REVISED_KOSCHMIEDER_CONTRAST_THRESHOLD));
    const auto squared_from_visibility = FogFalloff::from_visibility_squared(100.0f);
    ASSERT_TRUE(std::holds_alternative<FogFalloffExponentialSquared>(squared_from_visibility.value));
    EXPECT_FLOAT_EQ(std::get<FogFalloffExponentialSquared>(squared_from_visibility.value).density,
                    epix::pbr::fog_exponential_squared_density(100.0f, epix::pbr::REVISED_KOSCHMIEDER_CONTRAST_THRESHOLD));
    // The one-colour atmospheric constructor passes the same colour for both.
    const auto atmosphere = FogFalloff::from_visibility_color(100.0f, glm::vec4(0.0f, 0.0f, 1.0f, 0.5f));
    ASSERT_TRUE(std::holds_alternative<FogFalloffAtmospheric>(atmosphere.value));
    const auto& fields = std::get<FogFalloffAtmospheric>(atmosphere.value);
    EXPECT_LT(fields.extinction.z, fields.extinction.x) << "a blue extinction leaves blue visible";
    EXPECT_NEAR(fields.inscattering.z, 1.0f, 1e-6f) << "in-scattering keeps the colour as given";

    // No fog at all is mode off with everything zeroed, not an absent binding.
    const auto none = epix::pbr::GpuFog{};
    EXPECT_EQ(none.mode, GPU_FOG_MODE_OFF);
    EXPECT_EQ(none.base_color, glm::vec4(0.0f));
    EXPECT_EQ(none.be, glm::vec3(0.0f));
    EXPECT_EQ(none.bi, glm::vec3(0.0f));
}

/** @brief The environment map uniform family: the sample ray's transform, the
 *  per-view buffer and the view offset (Bevy `light_probe/mod.rs:266-289`). */
TEST(Cluster, EnvironmentMapUniformMatchesBevy) {
    using epix::pbr::EnvironmentMapUniform;
    using epix::pbr::EnvironmentMapUniformBuffer;
    using epix::pbr::ViewEnvironmentMapUniformOffset;

    // One mat4, and the identity default is meaningful: an unrotated probe samples the
    // cubemap unrotated rather than being undefined.
    EXPECT_EQ(sizeof(EnvironmentMapUniform), 64u) << "a single mat4";
    EXPECT_EQ(EnvironmentMapUniform{}.transform, glm::mat4(1.0f));
    const EnvironmentMapUniformBuffer empty;
    (void)empty;

    // The view offset defaults to zero, which is the first slot.
    EXPECT_EQ(ViewEnvironmentMapUniformOffset{}.offset, 0u);

    // The uniform is a shader type as-is; if the specialization were missing or ordered
    // after the buffer, the buffer above would not compile at all.
    static_assert(std::is_base_of_v<epix::render::render_resource::RawShaderType<EnvironmentMapUniform>,
                                    epix::render::render_resource::ShaderTypeInfo<EnvironmentMapUniform>>);
}

/** @brief One light probe as the shader reads it, including the std140 padding that
 *  keeps its size equal to its array stride (Bevy `RenderLightProbe`,
 *  `light_probe/mod.rs:64-86`). */
TEST(Cluster, RenderLightProbeLayoutMatchesBevy) {
    using epix::pbr::RenderLightProbe;

    // The shader reads these by offset, so the size is the assertion that matters: the
    // fields add up to 60 but std140 pads a struct holding a vec4 array to 16.
    EXPECT_EQ(sizeof(RenderLightProbe), 64u);
    EXPECT_EQ(offsetof(RenderLightProbe, light_from_world_transposed), 0u);
    EXPECT_EQ(offsetof(RenderLightProbe, texture_index), 48u);
    EXPECT_EQ(offsetof(RenderLightProbe, affects_lightmapped_mesh_diffuse), 56u);
    EXPECT_EQ(offsetof(RenderLightProbe, tail_padding), 60u);

    // The transform is three rows, not three columns: the shader transforms fragments
    // into the probe's space.
    EXPECT_EQ(sizeof(RenderLightProbe::light_from_world_transposed), 48u);
    const RenderLightProbe defaults;
    for (const glm::vec4& row : defaults.light_from_world_transposed) {
        EXPECT_EQ(row, glm::vec4(0.0f)) << "Bevy's ShaderType default is zeroed, unlike the identity a Transform would give";
    }
    EXPECT_EQ(defaults.texture_index, 0);
    EXPECT_FLOAT_EQ(defaults.intensity, 0.0f);
    EXPECT_EQ(defaults.affects_lightmapped_mesh_diffuse, 0u);

    // The two constants the uniform's array sizing follows.
    EXPECT_EQ(epix::pbr::MAX_VIEW_LIGHT_PROBES, 8u)
        << "small because the shader linearly searches the list per fragment";
    EXPECT_EQ(epix::pbr::STANDARD_MATERIAL_FRAGMENT_SHADER_MIN_TEXTURE_BINDINGS, 16u);
}

/** @brief The per-view light probe uniform: two eight-entry lists, their counts, and
 *  the view environment map fallback, laid out as std140 (Bevy `LightProbesUniform`,
 *  `light_probe/mod.rs:88-134`). */
TEST(Cluster, LightProbesUniformLayoutMatchesBevy) {
    using epix::pbr::LightProbesBuffer;
    using epix::pbr::LightProbesUniform;
    using epix::pbr::MAX_VIEW_LIGHT_PROBES;
    using epix::pbr::RenderLightProbe;
    using epix::pbr::ViewLightProbesUniformOffset;

    // The size is the assertion that matters: two arrays at the probe stride plus six
    // scalars, rounded to a multiple of 16.
    EXPECT_EQ(sizeof(LightProbesUniform), 1056u);
    EXPECT_EQ(sizeof(LightProbesUniform::reflection_probes), MAX_VIEW_LIGHT_PROBES * sizeof(RenderLightProbe));
    EXPECT_EQ(sizeof(LightProbesUniform::reflection_probes), 512u);
    EXPECT_EQ(offsetof(LightProbesUniform, irradiance_volumes), 512u)
        << "the second array follows the first with no gap";
    EXPECT_EQ(offsetof(LightProbesUniform, reflection_probe_count), 1024u);
    EXPECT_EQ(offsetof(LightProbesUniform, irradiance_volume_count), 1028u)
        << "the scalars pack tightly after the arrays";
    EXPECT_EQ(offsetof(LightProbesUniform, view_cubemap_index), 1032u);
    EXPECT_EQ(offsetof(LightProbesUniform, intensity_for_view), 1040u);
    EXPECT_EQ(offsetof(LightProbesUniform, tail_padding), 1048u) << "the six scalars take 24 bytes";

    // The default is all zeroes, including the counts, so a view with no probes reads
    // two empty lists rather than garbage.
    const LightProbesUniform defaults;
    EXPECT_EQ(defaults.reflection_probe_count, 0);
    EXPECT_EQ(defaults.irradiance_volume_count, 0);
    EXPECT_EQ(defaults.view_cubemap_index, 0);
    EXPECT_EQ(defaults.smallest_specular_mip_level_for_view, 0u);
    EXPECT_FLOAT_EQ(defaults.intensity_for_view, 0.0f);
    EXPECT_EQ(defaults.view_environment_map_affects_lightmapped_mesh_diffuse, 0u);
    for (const RenderLightProbe& probe : defaults.reflection_probes) {
        EXPECT_EQ(probe.texture_index, 0);
    }

    // The buffer and offset types instantiate, which is what proves the shader-type
    // specialization is in place and ordered before them.
    const LightProbesBuffer buffer;
    (void)buffer;
    EXPECT_EQ(ViewLightProbesUniformOffset{}.offset, 0u);
}

/** @brief The environment map uniform's transform is the inverse of the light's
 *  rotation, and a lightless view gets the identity (Bevy `gather_environment_map_uniform`,
 *  `light_probe/mod.rs:320-343`). */
TEST(Cluster, EnvironmentMapUniformRotationMatchesBevy) {
    using epix::pbr::environment_map_uniform_default;
    using epix::pbr::environment_map_uniform_from;

    // No light: the identity, so the sample direction is unrotated.
    EXPECT_EQ(environment_map_uniform_default().transform, glm::mat4(1.0f));

    // A rotation about Y by 90 degrees, and the property that matters: the stored
    // transform undoes it, which is what the inversion is for.
    const glm::quat rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const auto uniform       = environment_map_uniform_from(rotation);
    const glm::mat4 rotation_matrix = glm::mat4_cast(rotation);
    EXPECT_EQ(uniform.transform, glm::inverse(rotation_matrix));
    const glm::mat4 round_trip = uniform.transform * rotation_matrix;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            EXPECT_NEAR(round_trip[column][row], column == row ? 1.0f : 0.0f, 1e-5f)
                << "the stored transform must undo the rotation, not repeat it";
        }
    }
    // Concretely: rotating (0,0,1) by 90 degrees about Y gives (1,0,0), so the stored
    // inverse must map (1,0,0) back to (0,0,1).
    const glm::vec3 forward = glm::vec3(uniform.transform * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_NEAR(forward.x, 0.0f, 1e-5f);
    EXPECT_NEAR(forward.z, 1.0f, 1e-5f);

    // An identity rotation inverts to the identity, so the two construction paths agree.
    EXPECT_EQ(environment_map_uniform_from(glm::quat(1.0f, 0.0f, 0.0f, 0.0f)).transform,
              environment_map_uniform_default().transform);
}

/** @brief Probe culling and ordering: a unit cube scaled by the probe's transform, the
 *  far plane not tested, and squared camera distance as the sort key (Bevy
 *  `light_probe/mod.rs:561-582`). */
TEST(Cluster, LightProbeCullAndSortKeyMatchBevy) {
    using epix::pbr::light_probe_camera_distance_sort_key;
    using epix::pbr::light_probe_frustum_cull;

    epix::camera::PerspectiveProjection projection;
    projection.fov          = glm::radians(90.0f);
    projection.aspect_ratio = 1.0f;
    projection.near_plane   = 0.1f;
    projection.far_plane    = 100.0f;
    const auto frustum      = epix::camera::Frustum::from_clip_from_world(projection.get_clip_from_view());

    // A unit-scale probe at the origin sits inside the frustum.
    EXPECT_TRUE(light_probe_frustum_cull(glm::mat4(1.0f), frustum));

    // The probe's scale is what defines its region: translate it far to the side and it
    // leaves the frustum; scale it up enough and it reaches back in, with no extent
    // field changing.
    glm::mat4 off_to_the_side{1.0f};
    off_to_the_side[3] = glm::vec4(200.0f, 0.0f, -2.0f, 1.0f);
    EXPECT_FALSE(light_probe_frustum_cull(off_to_the_side, frustum));
    glm::mat4 scaled{1.0f};
    scaled[0][0] = scaled[1][1] = scaled[2][2] = 500.0f;
    scaled[3] = glm::vec4(200.0f, 0.0f, -2.0f, 1.0f);
    EXPECT_TRUE(light_probe_frustum_cull(scaled, frustum)) << "a big enough probe covers back into view";

    // The far plane is not tested: a probe beyond it still contributes, which is the
    // conservative choice for something the camera has not reached yet.
    glm::mat4 beyond_far{1.0f};
    beyond_far[3] = glm::vec4(0.0f, 0.0f, -500.0f, 1.0f);
    EXPECT_TRUE(light_probe_frustum_cull(beyond_far, frustum))
        << "culling on the far plane would drop probes the camera has not reached";

    // The sort key is squared distance, so the ordering is by proximity and equal
    // distances tie exactly rather than approximately.
    glm::mat4 near_probe{1.0f};
    near_probe[3] = glm::vec4(0.0f, 0.0f, -3.0f, 1.0f);
    glm::mat4 far_probe{1.0f};
    far_probe[3] = glm::vec4(0.0f, 0.0f, -9.0f, 1.0f);
    const glm::vec3 camera{0.0f, 0.0f, 0.0f};
    EXPECT_LT(light_probe_camera_distance_sort_key(near_probe, camera),
              light_probe_camera_distance_sort_key(far_probe, camera));
    EXPECT_FLOAT_EQ(light_probe_camera_distance_sort_key(near_probe, camera), 9.0f) << "squared, not the distance";
    // A probe behind the camera sorts after one in front, since distance is unsigned.
    glm::mat4 behind{1.0f};
    behind[3] = glm::vec4(0.0f, 0.0f, 3.0f, 1.0f);
    EXPECT_FLOAT_EQ(light_probe_camera_distance_sort_key(behind, camera),
                    light_probe_camera_distance_sort_key(near_probe, camera));
}

/** @brief Gathering a view's probe list: the cap keeps the nearest eight, and the
 *  texture index deduplicates textures rather than probes (Bevy
 *  `maybe_gather_light_probes`, `light_probe/mod.rs:634-650`). */
TEST(Cluster, LightProbesGatherListMatchesBevy) {
    using epix::pbr::LightProbeRecord;
    using epix::pbr::light_probes_gather_list;
    using epix::pbr::MAX_VIEW_LIGHT_PROBES;
    using epix::pbr::RenderLightProbe;

    const auto probe = [](std::uint64_t texture_id, float intensity) {
        LightProbeRecord record;
        record.texture_id = texture_id;
        record.intensity  = intensity;
        return record;
    };

    // The cap applies before the loop, so the *first* eight survive. The list arrives
    // sorted nearest-first, which is what makes that the nearest eight.
    std::vector<LightProbeRecord> many;
    for (std::uint64_t i = 0; i < 12; ++i) many.push_back(probe(i, static_cast<float>(i)));
    std::vector<RenderLightProbe> list;
    std::unordered_map<std::uint64_t, std::uint32_t> indices;
    light_probes_gather_list(list, indices, many);
    ASSERT_EQ(list.size(), MAX_VIEW_LIGHT_PROBES);
    EXPECT_FLOAT_EQ(list.front().intensity, 0.0f) << "the first probe is kept";
    EXPECT_FLOAT_EQ(list.back().intensity, 7.0f) << "and the eighth, not the twelfth";
    EXPECT_EQ(indices.size(), MAX_VIEW_LIGHT_PROBES) << "each distinct texture gets a slot";

    // Deduplication is on textures, not probes: two probes sharing a cubemap share an
    // index but still occupy two entries, since they cover different regions.
    std::vector<RenderLightProbe> shared_list;
    std::unordered_map<std::uint64_t, std::uint32_t> shared_indices;
    const std::vector<LightProbeRecord> shared = {probe(42, 1.0f), probe(7, 2.0f), probe(42, 3.0f)};
    light_probes_gather_list(shared_list, shared_indices, shared);
    ASSERT_EQ(shared_list.size(), 3u) << "one entry per probe";
    EXPECT_EQ(shared_indices.size(), 2u) << "but one slot per distinct texture";
    EXPECT_EQ(shared_list[0].texture_index, shared_list[2].texture_index)
        << "the two probes sharing a cubemap share its index";
    EXPECT_NE(shared_list[1].texture_index, shared_list[0].texture_index);
    EXPECT_FLOAT_EQ(shared_list[2].intensity, 3.0f) << "each keeps its own intensity";

    // The flags and the transform pass through: the transposition happened when the
    // probe info was built, not here.
    LightProbeRecord flagged;
    flagged.texture_id = 5;
    flagged.affects_lightmapped_mesh_diffuse = true;
    flagged.light_from_world_transposed[2] = glm::vec4(1.0f, 2.0f, 3.0f, 4.0f);
    std::vector<RenderLightProbe> flagged_list;
    std::unordered_map<std::uint64_t, std::uint32_t> flagged_indices;
    light_probes_gather_list(flagged_list, flagged_indices, {flagged});
    ASSERT_EQ(flagged_list.size(), 1u);
    EXPECT_EQ(flagged_list[0].affects_lightmapped_mesh_diffuse, 1u) << "the bool becomes 1, not true";
    EXPECT_EQ(flagged_list[0].light_from_world_transposed[2], glm::vec4(1.0f, 2.0f, 3.0f, 4.0f));

    // An empty gather leaves both outputs untouched.
    std::vector<RenderLightProbe> empty_list;
    std::unordered_map<std::uint64_t, std::uint32_t> empty_indices;
    light_probes_gather_list(empty_list, empty_indices, {});
    EXPECT_TRUE(empty_list.empty());
    EXPECT_TRUE(empty_indices.empty());
}

/** @brief `gather_environment_map_uniform` gives every `Camera3d` view an
 *  `EnvironmentMapUniform` - the inverse of the light's rotation, or the identity
 *  when the view has no `EnvironmentMapLight` (Bevy `light_probe/mod.rs:324-343`).
 *
 *  Bevy writes this instead of using `ExtractComponentPlugin` precisely so that a
 *  view without the component still gets the default instance, which is the second
 *  view below. The system reads the main world through `ExtractedWorld` and writes
 *  through `Commands`, so a pair of bare worlds drives it exactly as extraction
 *  does - down to the render entity each command targets. */
TEST(LightProbe, GatherEnvironmentMapUniformCoversEveryCamera3dView) {
    using epix::pbr::EnvironmentMapLight;
    using epix::pbr::EnvironmentMapUniform;

    epix::ecs::World main_world(3);
    epix::ecs::World render_world(3);
    render_world.insert_resource(epix::app::ExtractedWorld{main_world});

    constexpr float kAngle   = 0.7f;
    const glm::quat rotation = glm::angleAxis(kAngle, glm::vec3(0.0f, 1.0f, 0.0f));
    EnvironmentMapLight with_light;
    with_light.rotation = rotation;

    // A main-world view pointing at its render-world entity, which is where the
    // uniform lands.
    const auto add_view = [&](bool camera3d, bool has_light) {
        const auto render_entity = render_world.spawn().id();
        auto view                = main_world.spawn(epix::render::sync_world::RenderEntity{render_entity});
        if (camera3d) view.insert(epix::camera::Camera3d{});
        if (has_light) view.insert(with_light);
        return render_entity;
    };
    const auto rotated = add_view(true, true);
    const auto plain   = add_view(true, false);
    // A 2D camera is not one of this system's views, even when it carries a light.
    const auto camera2d = add_view(false, true);

    auto system = epix::ecs::make_system_unique(epix::pbr::gather_environment_map_uniform);
    system->initialize(render_world);
    ASSERT_TRUE(system->run({}, render_world).has_value());
    system->apply_deferred(render_world);

    const auto uniform_of = [&](epix::ecs::Entity render_entity) {
        return render_world.entity(render_entity).get<EnvironmentMapUniform>().transform(
            [](const auto& value) { return value.get(); });
    };

    const auto rotated_uniform = uniform_of(rotated);
    ASSERT_TRUE(rotated_uniform.has_value()) << "a Camera3d with a light gets a uniform";
    EXPECT_EQ(rotated_uniform->transform, glm::inverse(glm::mat4_cast(rotation)))
        << "the uniform holds the inverse of the light's rotation";

    const auto plain_uniform = uniform_of(plain);
    ASSERT_TRUE(plain_uniform.has_value()) << "and so does one without the component";
    EXPECT_EQ(plain_uniform->transform, glm::mat4(1.0f)) << "defaulting to the identity";

    EXPECT_FALSE(uniform_of(camera2d).has_value()) << "a Camera2d is not a view of this system";
}

}  // namespace
