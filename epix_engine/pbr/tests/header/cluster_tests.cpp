#include <epix/pbr/ambient_light.hpp>
#include <epix/pbr/cascade.hpp>
#include <epix/pbr/cluster.hpp>
#include <epix/pbr/directional_light.hpp>
#include <epix/pbr/gpu_lights.hpp>
#include <epix/pbr/light_cluster.hpp>
#include <epix/pbr/light_meta.hpp>
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
    const glm::mat4 projection = glm::perspective(glm::radians(60.0f), screen_size.x / screen_size.y, 0.1f, 100.0f);
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
    const glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(60.0f), screen.x / screen.y, z_far, z_near);
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
    const glm::mat4 ortho = glm::ortho(-1.0f, 1.0f, -0.75f, 0.75f, z_near, z_far);
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
    const glm::mat4 projection     = glm::perspectiveRH_ZO(glm::radians(60.0f), screen.x / screen.y, z_far, z_near);
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
    const glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(60.0f), screen.x / screen.y, z_far, z_near);
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

}  // namespace
