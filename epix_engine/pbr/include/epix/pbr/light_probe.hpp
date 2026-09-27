#pragma once

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstdint>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <epix/render/bind_group_layout_entries.hpp>
#include <glm/glm.hpp>
#endif
#include <epix/pbr/probe.hpp>

namespace epix::pbr {

/** @brief The most probes of each kind a view considers (Bevy `MAX_VIEW_LIGHT_PROBES`,
 *  `light_probe/mod.rs:47-51`).
 *
 *  Small on purpose: the fragment shader linearly searches these lists per fragment. */
EPIX_EXPORT inline constexpr std::size_t MAX_VIEW_LIGHT_PROBES = 8;

/** @brief Texture bindings the fragment shader uses, not counting environment maps or
 *  irradiance volumes (Bevy `STANDARD_MATERIAL_FRAGMENT_SHADER_MIN_TEXTURE_BINDINGS`,
 *  `light_probe/mod.rs:53-55`). Used to work out how many probes fit in the binding
 *  arrays. */
EPIX_EXPORT inline constexpr std::size_t STANDARD_MATERIAL_FRAGMENT_SHADER_MIN_TEXTURE_BINDINGS = 16;

/** @brief One light probe as the shader reads it (Bevy `RenderLightProbe`,
 *  `light_probe/mod.rs:64-86`).
 *
 *  The transform is stored **transposed**, as three rows, because that is the form the
 *  bounding-box test in the shader wants: it transforms the fragment into the probe's
 *  space rather than the other way round.
 *
 *  The layout is 64 bytes, not the 60 the fields add up to, because a struct holding a
 *  `vec4` array aligns to 16 in std140 - and 64 is the stride an array of these needs
 *  inside the per-view uniform. The padding is explicit for the same reason as
 *  `GpuFog`'s: glm's `vec4` is not 16-byte aligned by default, so the struct would
 *  otherwise come out at 60 and every probe after the first would be misread. */
EPIX_EXPORT struct RenderLightProbe {
    /** @brief The world-to-probe transform, transposed into three rows. */
    std::array<glm::vec4, 3> light_from_world_transposed{};
    /** @brief The probe's index in the texture array or arrays. */
    std::int32_t texture_index = 0;
    /** @brief Scale applied to the light this probe contributes. */
    float intensity = 0.0f;
    /** @brief Whether this probe adds to the diffuse contribution for lightmapped
     *  meshes: 1 if it does, 0 otherwise. */
    std::uint32_t affects_lightmapped_mesh_diffuse = 0;
    /** @brief Explicit std140 tail padding; see the class comment. */
    std::uint32_t tail_padding = 0;
};
static_assert(sizeof(RenderLightProbe) == 64, "RenderLightProbe must match the shader's layout");
static_assert(offsetof(RenderLightProbe, light_from_world_transposed) == 0);
static_assert(offsetof(RenderLightProbe, texture_index) == 48);
static_assert(offsetof(RenderLightProbe, intensity) == 52);
static_assert(offsetof(RenderLightProbe, affects_lightmapped_mesh_diffuse) == 56);
static_assert(offsetof(RenderLightProbe, tail_padding) == 60);

}  // namespace epix::pbr

namespace epix::render::render_resource {
/** @brief `RenderLightProbe` is uploaded as raw bytes; the struct is already padded to
 *  the shader's layout with every offset asserted, so a derived layout has nothing to
 *  add. */
template <>
struct ShaderTypeInfo<::epix::pbr::RenderLightProbe> : RawShaderType<::epix::pbr::RenderLightProbe> {};
}  // namespace epix::render::render_resource

namespace epix::pbr {

/** @brief The light probes a view takes into account, as the shader reads them (Bevy
 *  `LightProbesUniform`, `light_probe/mod.rs:88-125`).
 *
 *  Both lists are sorted nearest to farthest from the camera and padded out to
 *  `MAX_VIEW_LIGHT_PROBES` entries, with the counts saying how many are real: the
 *  shader walks the list in order and stops at the count, so the entries past it are
 *  never read but still occupy the layout.
 *
 *  The uniform's size follows from std140 rather than from the fields adding up. Each
 *  array element is a `RenderLightProbe` at its 64-byte stride, so the two arrays are
 *  512 bytes each; the six scalars then pack into 24 contiguous bytes, and the struct
 *  rounds up to a multiple of 16. The tail padding makes that explicit - without it the
 *  struct would come out at 1048 and the view environment map fields would sit in the
 *  wrong place for every array after the first. */
EPIX_EXPORT struct LightProbesUniform {
    std::array<RenderLightProbe, MAX_VIEW_LIGHT_PROBES> reflection_probes{};
    std::array<RenderLightProbe, MAX_VIEW_LIGHT_PROBES> irradiance_volumes{};
    /** @brief How many reflection probes in the list are real. */
    std::int32_t reflection_probe_count = 0;
    /** @brief How many irradiance volumes in the list are real. */
    std::int32_t irradiance_volume_count = 0;
    /** @brief The view's own environment maps, used when no probe contains the
     *  fragment. */
    std::int32_t view_cubemap_index = 0;
    /** @brief The smallest valid specular mip level for the view's cubemap. */
    std::uint32_t smallest_specular_mip_level_for_view = 0;
    /** @brief The intensity of the view's environment cubemap. */
    float intensity_for_view = 0.0f;
    /** @brief Whether the view's environment map affects lightmapped meshes: 1 if it
     *  does, 0 otherwise. */
    std::uint32_t view_environment_map_affects_lightmapped_mesh_diffuse = 0;
    /** @brief Explicit std140 tail padding; see the class comment. */
    std::array<std::uint32_t, 2> tail_padding{};
};
static_assert(sizeof(LightProbesUniform) == 1056, "LightProbesUniform must match the shader's layout");
static_assert(offsetof(LightProbesUniform, reflection_probes) == 0);
static_assert(offsetof(LightProbesUniform, irradiance_volumes) == 512);
static_assert(offsetof(LightProbesUniform, reflection_probe_count) == 1024);
static_assert(offsetof(LightProbesUniform, view_cubemap_index) == 1032);
static_assert(offsetof(LightProbesUniform, tail_padding) == 1048);

}  // namespace epix::pbr

namespace epix::render::render_resource {
/** @brief `LightProbesUniform` is uploaded as raw bytes; the struct is already padded
 *  to the shader's std140 layout with its offsets asserted. */
template <>
struct ShaderTypeInfo<::epix::pbr::LightProbesUniform> : RawShaderType<::epix::pbr::LightProbesUniform> {};
}  // namespace epix::render::render_resource

namespace epix::pbr {

/** @brief One `LightProbesUniform` per view (Bevy `LightProbesBuffer`,
 *  `light_probe/mod.rs:127-129`). */
EPIX_EXPORT struct LightProbesBuffer {
    render::render_resource::DynamicUniformBuffer<LightProbesUniform> uniforms;
};

/** @brief A view's offset into the light probe buffer (Bevy
 *  `ViewLightProbesUniformOffset`, `light_probe/mod.rs:131-134`). */
EPIX_EXPORT struct ViewLightProbesUniformOffset {
    std::uint32_t offset = 0;
};

/** @brief The uniform built from an `EnvironmentMapLight` and read by the environment
 *  map shader (Bevy `EnvironmentMapUniform`, `light_probe/mod.rs:266-280`).
 *
 *  One matrix: the world-space transform of the sample ray for the cubemap. The
 *  identity default is meaningful rather than a placeholder - a probe with no rotation
 *  samples the cubemap unrotated. */
EPIX_EXPORT struct EnvironmentMapUniform {
    glm::mat4 transform{1.0f};
};

}  // namespace epix::pbr

namespace epix::render::render_resource {
/** @brief The uniform is one `mat4` uploaded as raw bytes, so a derived layout has
 *  nothing to add. This must precede `EnvironmentMapUniformBuffer`, which instantiates
 *  `DynamicUniformBuffer<EnvironmentMapUniform>` and checks the `ShaderWritable`
 *  concept there - the same ordering fog's uniform needs. */
template <>
struct ShaderTypeInfo<::epix::pbr::EnvironmentMapUniform> : RawShaderType<::epix::pbr::EnvironmentMapUniform> {};
}  // namespace epix::render::render_resource

namespace epix::pbr {

/** @brief The environment map settings per view (Bevy `EnvironmentMapUniformBuffer`,
 *  `light_probe/mod.rs:282-284`). */
EPIX_EXPORT struct EnvironmentMapUniformBuffer {
    render::render_resource::DynamicUniformBuffer<EnvironmentMapUniform> uniforms;
};

/** @brief A view's offset into the environment map buffer (Bevy
 *  `ViewEnvironmentMapUniformOffset`, `light_probe/mod.rs:286-289`). */
EPIX_EXPORT struct ViewEnvironmentMapUniformOffset {
    std::uint32_t offset = 0;
};

/** @brief Extract each `Camera3d` view's environment map rotation into an
 *  `EnvironmentMapUniform`, defaulting to the identity when the view has no
 *  `EnvironmentMapLight` (Bevy `gather_environment_map_uniform`,
 *  `light_probe/mod.rs:324-343`, an `ExtractSchedule` system).
 *
 *  Bevy writes this rather than using `ExtractComponentPlugin` precisely so that a
 *  view without the component still gets the default instance - which is what makes
 *  `prepare_environment_uniform_buffer`'s offset-0 arm cover only views that
 *  extraction never saw. */
EPIX_EXPORT EPIX_API void gather_environment_map_uniform(
    app::Extract<ecs::Query<ecs::Item<::epix::render::sync_world::RenderEntity, ecs::Opt<const EnvironmentMapLight&>>,
                            ecs::With<::epix::camera::Camera3d>>> views,
    ecs::Commands commands);

/** @brief Writes one environment map uniform per view (Bevy
 *  `prepare_environment_uniform_buffer`, `light_probe/mod.rs:404-428`).
 *
 *  Note how this differs from fog's equivalent: a view *without* the component gets
 *  offset **0** rather than a written entry. Fog has to write a real entry for every
 *  view because "no fog" is a mode the shader reads; here the absence is the shader's
 *  business, and offset 0 is the identity transform the default uniform already holds.
 *  Writing the default would work too, but it would spend a uniform slot per view that
 *  has no environment map at all. */
EPIX_EXPORT EPIX_API void prepare_environment_uniform_buffer(
    ecs::Commands commands,
    ecs::Res<wgpu::Device> render_device,
    ecs::Res<wgpu::Queue> render_queue,
    ecs::ResMut<EnvironmentMapUniformBuffer> environment_uniform_buffer,
    ecs::Query<ecs::Item<ecs::Entity, ecs::Opt<const EnvironmentMapUniform&>>,
               ecs::With<::epix::render::view::ExtractedView>> views);

/** @brief The uniform a view's `EnvironmentMapLight` produces (Bevy
 *  `gather_environment_map_uniform`, `light_probe/mod.rs:320-343`).
 *
 *  The transform is the **inverse** of the light's rotation, not the rotation itself:
 *  the shader uses it to carry the sample direction out of the probe's space, so
 *  storing the inverse saves it from inverting per fragment. It is a full matrix rather
 *  than the quaternion because that is how the uniform is declared.
 *
 *  Between this and `prepare_environment_uniform_buffer`, every view ends up with an
 *  offset: a view that is a `Camera3d` gets a uniform here even without an
 *  `EnvironmentMapLight`, holding the identity, and the offset-0 arm there covers only
 *  views that extraction never saw at all. */
EPIX_EXPORT EPIX_API EnvironmentMapUniform environment_map_uniform_from(glm::quat rotation);

/** @brief The uniform for a view with no environment map light: the identity, so the
 *  sample direction passes through unrotated (Bevy `EnvironmentMapUniform::default`). */
EPIX_EXPORT inline EnvironmentMapUniform environment_map_uniform_default() { return EnvironmentMapUniform{}; }

/** @brief Whether a light probe is inside a view's frustum (Bevy
 *  `LightProbeInfo::frustum_cull`, `light_probe/mod.rs:561-573`).
 *
 *  The probe is tested as a **unit cube at its own origin**, so its scale is what
 *  defines the region it covers - a probe scaled to 4×4×4 covers that much space
 *  without any extent field. Testing one cube for every probe is also why the frustum
 *  needs no per-probe geometry.
 *
 *  Note the two flags: the near plane *is* tested and the far plane is not. A probe
 *  beyond the far plane still lights the scene, which is the conservative choice - if
 *  it were culled on the far plane, a probe the camera has not reached yet would stop
 *  contributing the moment the camera's far plane moved inside it. */
EPIX_EXPORT EPIX_API bool light_probe_frustum_cull(const glm::mat4& world_from_light,
                                                 const ::epix::camera::Frustum& view_frustum);

/** @brief A light probe's sort key: its **squared** distance from the camera (Bevy
 *  `LightProbeInfo::camera_distance_sort_key`, `light_probe/mod.rs:575-582`).
 *
 *  Squared because only the ordering matters and the square root is monotonic, and a
 *  float rather than Bevy's `FloatOrd` because that wrapper exists to make the sort
 *  total in the presence of NaN, which the sort here does not need. The lists come out
 *  nearest-first, which is the order the shader walks them in. */
EPIX_EXPORT EPIX_API float light_probe_camera_distance_sort_key(const glm::mat4& world_from_light,
                                                              glm::vec3 view_translation);

/** @brief What one gathered probe contributes to the uniform, standing in for Bevy's
 *  `LightProbeInfo<C>`, which is generic only because the asset ids differ per probe
 *  type. `texture_id` is that asset id, flattened to a number the way the
 *  render-world texture bookkeeping does. */
EPIX_EXPORT struct LightProbeRecord {
    std::array<glm::vec4, 3> light_from_world_transposed{};
    std::uint64_t texture_id              = 0;
    float intensity                       = 0.0f;
    bool affects_lightmapped_mesh_diffuse = false;
};

/** @brief Fills one of a view's probe lists from the gathered probes (Bevy
 *  `RenderViewLightProbes::maybe_gather_light_probes`,
 *  `light_probe/mod.rs:634-650`).
 *
 *  Two rules, and they are easy to confuse with each other:
 *
 *  - The cap is applied **before** the loop, so it keeps the *first*
 *    `MAX_VIEW_LIGHT_PROBES` probes - which is only meaningful because the list arrives
 *    sorted nearest-first. A cap applied after sorting by anything else would keep an
 *    arbitrary eight.
 *  - `texture_index` **deduplicates textures, not probes**. Two probes sharing a
 *    cubemap get the same index but still occupy two entries in the list, because they
 *    cover different regions of the scene and the shader has to test each one's
 *    bounds. Returning a list also makes the index assignment order-dependent, which is
 *    why the returned indices are per-view.
 *
 *  `indices` maps a texture id to its slot in the view's binding arrays and is updated
 *  in place, exactly as Bevy's `get_or_insert_cubemap` does. */
EPIX_EXPORT EPIX_API void light_probes_gather_list(std::vector<RenderLightProbe>& out,
                                                 std::unordered_map<std::uint64_t, std::uint32_t>& indices,
                                                 const std::vector<LightProbeRecord>& probes);

/** @brief The environment map layout entries: the diffuse and specular cubemap
 *  bindings, their sampler, and the uniform (Bevy
 *  `environment_map::get_bind_group_layout_entries`,
 *  `light_probe/environment_map.rs:150-169`).
 *
 *  The two cubemaps are binding arrays of `MAX_VIEW_LIGHT_PROBES` when the device
 *  supports arrays, and single textures otherwise - which is the only difference
 *  between the two shapes. They land at the view layout's binding-array group
 *  bindings 0 and 1, and the uniform at binding 17 of the main group. */
EPIX_EXPORT inline std::array<render::render_resource::BindGroupLayoutEntryBuilder, 4>
environment_map_bind_group_layout_entries(bool binding_arrays_are_usable) {
    using render::render_resource::binding_types::sampler;
    using render::render_resource::binding_types::texture_cube;
    using render::render_resource::binding_types::uniform_buffer;

    auto texture_cube_binding = texture_cube(wgpu::TextureSampleType::eFloat);
    if (binding_arrays_are_usable) texture_cube_binding.count(static_cast<std::uint32_t>(MAX_VIEW_LIGHT_PROBES));

    return {
        texture_cube_binding,
        texture_cube_binding,
        sampler(wgpu::SamplerBindingType::eFiltering),
        uniform_buffer(true, sizeof(EnvironmentMapUniform)).visibility(wgpu::ShaderStage::eFragment),
    };
}

/** @brief The irradiance volume layout entries: the voxel texture and its sampler
 *  (Bevy `irradiance_volume::get_bind_group_layout_entries`,
 *  `light_probe/irradiance_volume.rs:277-294`).
 *
 *  Like the environment maps, the texture is an array of `MAX_VIEW_LIGHT_PROBES`
 *  when the device supports arrays. The two entries land at the binding-array
 *  group's bindings 3 and 4. */
EPIX_EXPORT inline std::array<render::render_resource::BindGroupLayoutEntryBuilder, 2>
irradiance_volume_bind_group_layout_entries(bool binding_arrays_are_usable) {
    using render::render_resource::binding_types::sampler;
    using render::render_resource::binding_types::texture_3d;

    auto texture_3d_binding = texture_3d(wgpu::TextureSampleType::eFloat);
    if (binding_arrays_are_usable) texture_3d_binding.count(static_cast<std::uint32_t>(MAX_VIEW_LIGHT_PROBES));

    return {texture_3d_binding, sampler(wgpu::SamplerBindingType::eFiltering)};
}

}  // namespace epix::pbr
