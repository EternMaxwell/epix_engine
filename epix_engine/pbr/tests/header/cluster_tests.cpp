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

}  // namespace
