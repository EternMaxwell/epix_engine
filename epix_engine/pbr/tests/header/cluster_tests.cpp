#include <epix/pbr/ambient_light.hpp>
#include <epix/pbr/cascade.hpp>
#include <epix/pbr/cluster.hpp>
#include <epix/pbr/directional_light.hpp>
#include <epix/pbr/light_cluster.hpp>
#include <epix/pbr/point_light.hpp>
#include <epix/pbr/probe.hpp>
#include <epix/pbr/spot_light.hpp>
#include <epix/pbr/volumetric.hpp>
#include <gtest/gtest.h>
#include <cstdint>
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
}

/** @brief The uniform form is a full array; the three storage forms are
 *  runtime-sized, so they report a zero minimum and a written size that follows
 *  the data (Bevy `#[shader(size(runtime))]`). */
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

    EXPECT_EQ(ShaderTypeInfo<GpuClusterableObjectsStorage>::shader_size, 0u)
        << "a struct whose only member is the runtime array has a zero minimum";
    EXPECT_EQ(ShaderTypeInfo<GpuClusterableObjectIndexListsStorage>::shader_size, 0u);
    EXPECT_EQ(ShaderTypeInfo<GpuClusterOffsetsAndCountsStorage>::shader_size, 0u);

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
    EXPECT_EQ(config.kind, ClusterConfig::Kind::None) << "Bevy's ClusterConfig default disables clustering";
    EXPECT_FALSE(config.dynamic_resizing);

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
    using epix::pbr::VERY_LARGE_CINEMA_LIGHT;

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
    using epix::pbr::lux::AMBIENT_DAYLIGHT;
    using epix::pbr::validated_shadow_map_size;

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
    EXPECT_EQ(validated_shadow_map_size(2048), 2048u) << "a power of two is left alone";
    EXPECT_EQ(validated_shadow_map_size(3000), 4096u) << "anything else rounds up";
    EXPECT_EQ(validated_shadow_map_size(1), 1u);

    const DirectionalLightTexture texture{};
    EXPECT_FALSE(texture.tiled) << "Bevy centres a single tile by default";
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
    EXPECT_EQ(z_config.far_z_mode, ClusterFarZMode::MaxClusterableObjectRange);

    const ClusteredDecal decal{};
    EXPECT_FALSE(decal.base_color_texture.has_value());
    EXPECT_FALSE(decal.normal_map_texture.has_value());
    EXPECT_FALSE(decal.metallic_roughness_texture.has_value());
    EXPECT_FALSE(decal.emissive_texture.has_value());
    EXPECT_EQ(decal.tag, 0u);
}

}  // namespace
