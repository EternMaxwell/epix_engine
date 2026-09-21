#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <algorithm>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
#include <webgpu/webgpu.hpp>
#include <epix/common.hpp>
#endif
#include <epix/camera/visibility.hpp>
#include <epix/pbr/cascade.hpp>
#include <epix/pbr/gpu_lights.hpp>
#include <epix/pbr/light_cluster.hpp>

namespace epix::pbr {

/** @brief Holds one `GpuLights` per view, uploaded once per frame (Bevy
 *  `LightMeta`, `bevy_pbr/src/render/light.rs:685-688`).
 *
 *  `prepare_lights` pushes a view's lights and stamps the view with the entry's
 *  byte offset, which the mesh view bind group passes as its dynamic offset. As
 *  with the engine's other dynamic uniform buffers, the buffer needs the device's
 *  alignment at construction. */
EPIX_EXPORT struct LightMeta {
    render::render_resource::DynamicUniformBuffer<GpuLights> view_gpu_lights;

    /** @brief The default-constructed meta carries an unaligned buffer, so it can
     *  be filled but not uploaded until it is aligned for a device. */
    LightMeta() = default;
    explicit LightMeta(std::size_t dynamic_offset_alignment)
        : view_gpu_lights(
              render::render_resource::DynamicUniformBuffer<GpuLights>::new_with_alignment(dynamic_offset_alignment)) {}
};

/** @brief This view's `GpuLights` entry offset in `LightMeta` (Bevy
 *  `ViewLightsUniformOffset`, `light.rs:680-683`). */
EPIX_EXPORT struct ViewLightsUniformOffset {
    std::uint32_t offset = 0;
};

/** @brief The shadow cascade views associated with a camera (Bevy
 *  `ViewLightEntities`, `light.rs:670-678`). Despite the name it holds the
 *  cascade views, not the lights. */
EPIX_EXPORT struct ViewLightEntities {
    std::vector<ecs::Entity> lights;
};

/** @brief The depth textures a view's shadow maps render into (Bevy
 *  `ViewShadowBindings`, `light.rs:657-663`), one pair for point lights' cubemaps
 *  and one for directional lights' cascades. */
EPIX_EXPORT struct ViewShadowBindings {
    wgpu::Texture point_light_depth_texture;
    wgpu::TextureView point_light_depth_texture_view;
    wgpu::Texture directional_light_depth_texture;
    wgpu::TextureView directional_light_depth_texture_view;
};

/** @brief What a shadow view belongs to (Bevy `LightEntity`,
 *  `light.rs:690-703`): a directional light's cascade, a point light's cubemap
 *  face, or a spot light. */
EPIX_EXPORT struct LightEntity {
    enum class Kind {
        Directional,
        Point,
        Spot,
    } kind = Kind::Spot;

    /** @brief The light this shadow view belongs to. */
    ecs::Entity light_entity{};
    /** @brief Which cascade, when `kind` is `Directional`. */
    std::size_t cascade_index = 0;
    /** @brief Which cubemap face, when `kind` is `Point`. */
    std::size_t face_index = 0;
};

/** @brief The bits a point light's `flags` word carries (Bevy `PointLightFlags`,
 *  `render/light.rs:112-122`); these must match `mesh_view_types.wgsl`.
 *
 *  `SPOT_LIGHT_Y_NEGATIVE` is the odd one: Bevy extracts spot lights *as* point
 *  lights and carries their cone in `spot_light_angles`, so the shader needs to
 *  know which way the cone's Y axis points. */
EPIX_EXPORT struct PointLightFlags {
    static constexpr std::uint32_t SHADOWS_ENABLED                  = 1u << 0;
    static constexpr std::uint32_t SPOT_LIGHT_Y_NEGATIVE            = 1u << 1;
    static constexpr std::uint32_t VOLUMETRIC                       = 1u << 2;
    static constexpr std::uint32_t AFFECTS_LIGHTMAPPED_MESH_DIFFUSE = 1u << 3;
    static constexpr std::uint32_t NONE                             = 0;
    static constexpr std::uint32_t UNINITIALIZED                    = 0xFFFF;
};

/** @brief A point light as extracted for the render world (Bevy
 *  `ExtractedPointLight`, `render/light.rs:69-86`).
 *
 *  Bevy stores a `LinearRgba`, which is a linear `glm::vec4` here. Spot lights
 *  are extracted into this same type, with their cone's inner and outer angles in
 *  `spot_light_angles` - which is why a point light has a spot-light flag in the
 *  shader. */
EPIX_EXPORT struct ExtractedPointLight {
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    /** @brief Luminous intensity in lumens per steradian. */
    float intensity = 0.0f;
    float range     = 0.0f;
    float radius    = 0.0f;
    /** @brief The light's global transform, unless it is a spot light, in which
     *  case Bevy rebuilds it from the translation alone. */
    glm::mat4 transform{1.0f};
    bool shadows_enabled     = false;
    float shadow_depth_bias  = 0.0f;
    float shadow_normal_bias = 0.0f;
    float shadow_map_near_z  = 0.0f;
    /** @brief The spot-light cone's inner and outer angles, when this point light
     *  is really a spot light. */
    std::optional<std::pair<float, float>> spot_light_angles;
    bool volumetric                = false;
    bool soft_shadows_enabled      = false;
    /** @brief Whether this light contributes diffuse light to lightmapped
     *  meshes. */
    bool affects_lightmapped_mesh_diffuse = true;
};

/** @brief A directional light as extracted for the render world (Bevy
 *  `ExtractedDirectionalLight`, `render/light.rs:88-109`).
 *
 *  `cascades` and `frusta` are keyed by view, since a light's cascades are built
 *  per view. */
EPIX_EXPORT struct ExtractedDirectionalLight {
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    /** @brief Illuminance in lux. */
    float illuminance = 0.0f;
    glm::mat4 transform{1.0f};
    bool shadows_enabled = false;
    bool volumetric      = false;
    bool affects_lightmapped_mesh_diffuse = true;
    float shadow_depth_bias  = 0.0f;
    float shadow_normal_bias = 0.0f;
    /** @brief The light's cascade configuration, which the shadow pass reads. */
    CascadeShadowConfig cascade_shadow_config{};
    /** @brief The cascades built for each view. */
    std::unordered_map<ecs::Entity, std::vector<Cascade>> cascades;
    /** @brief The shadow frusta built for each view. */
    std::unordered_map<ecs::Entity, std::vector<camera::Frustum>> frusta;
    /** @brief The render layers the light is visible through. */
    std::uint32_t render_layers = 0;
    /** @brief The soft shadow size, when PCSS is in use. */
    std::optional<float> soft_shadow_size;
    /** @brief Whether this light uses two-phase occlusion culling. */
    bool occlusion_culling = false;
    float sun_disk_angular_size = 0.0f;
    float sun_disk_intensity    = 0.0f;
};

/** @brief How many lights of each kind a frame has, after the device's limits
 *  (Bevy `prepare_lights`, `render/light.rs:806-853`).
 *
 *  Each count is capped by what the device can bind: point lights and
 *  directional cascades by the cubemap and array-layer limits respectively, and
 *  spot lights by whatever layers the directional shadow maps have not already
 *  taken, since they share one texture array. */
EPIX_EXPORT struct LightCounts {
    /** @brief Point lights, i.e. those without spot angles. */
    std::size_t point_lights = 0;
    std::size_t point_light_volumetric_enabled = 0;
    std::size_t point_light_shadow_maps = 0;
    /** @brief Spot lights, i.e. point lights carrying spot angles. */
    std::size_t spot_lights = 0;
    std::size_t spot_light_volumetric_enabled = 0;
    std::size_t spot_light_shadow_maps = 0;
    /** @brief Directional lights, counted only up to `MAX_DIRECTIONAL_LIGHTS`. */
    std::size_t directional_volumetric_enabled = 0;
    std::size_t directional_shadow_enabled = 0;
};

/** @brief Count a frame's lights against the device's texture limits (Bevy
 *  `prepare_lights`, `light.rs:806-853`).
 *
 *  `max_texture_array_layers` and `max_texture_cubes` come from the device
 *  (`max_texture_cubes` is a sixth of the layer count, since a cubemap is six
 *  layers). The directional counts are taken first because the spot-light counts
 *  subtract the layers the directional shadow maps occupy. */
EPIX_EXPORT inline LightCounts count_lights(const std::vector<ExtractedPointLight>& point_lights,
                                            const std::vector<ExtractedDirectionalLight>& directional_lights,
                                            std::size_t max_texture_array_layers, std::size_t max_texture_cubes) {
    LightCounts counts;

    const auto is_spot = [](const ExtractedPointLight& light) { return light.spot_light_angles.has_value(); };
    const auto capped  = [](std::size_t value, std::size_t cap) { return std::min(value, cap); };

    std::size_t points = 0;
    std::size_t point_volumetric = 0;
    std::size_t point_shadows = 0;
    std::size_t spots = 0;
    std::size_t spot_volumetric = 0;
    std::size_t spot_shadows = 0;
    for (const ExtractedPointLight& light : point_lights) {
        if (is_spot(light)) {
            ++spots;
            if (light.volumetric) ++spot_volumetric;
            if (light.shadows_enabled) ++spot_shadows;
        } else {
            ++points;
            if (light.volumetric) ++point_volumetric;
            if (light.shadows_enabled) ++point_shadows;
        }
    }

    const std::size_t directional_limit = max_texture_array_layers / MAX_CASCADES_PER_LIGHT;
    std::size_t directional_volumetric = 0;
    std::size_t directional_shadows    = 0;
    for (std::size_t i = 0; i < std::min(directional_lights.size(), MAX_DIRECTIONAL_LIGHTS); ++i) {
        if (directional_lights[i].volumetric) ++directional_volumetric;
        if (directional_lights[i].shadows_enabled) ++directional_shadows;
    }

    counts.point_lights                      = points;
    counts.point_light_volumetric_enabled    = capped(point_volumetric, max_texture_cubes);
    counts.point_light_shadow_maps           = capped(point_shadows, max_texture_cubes);
    counts.directional_volumetric_enabled    = capped(directional_volumetric, directional_limit);
    counts.directional_shadow_enabled        = capped(directional_shadows, directional_limit);
    // Spot lights share the texture array with the directional shadow maps.
    const std::size_t spot_limit = max_texture_array_layers - counts.directional_shadow_enabled * MAX_CASCADES_PER_LIGHT;
    counts.spot_lights                    = capped(spots, spot_limit);
    counts.spot_light_volumetric_enabled  = capped(spot_volumetric, spot_limit);
    counts.spot_light_shadow_maps         = capped(spot_shadows, spot_limit);
    return counts;
}

/** @brief The clusterable form of an extracted point light (Bevy
 *  `point_or_spot_light_to_clusterable`, `light.rs:2362-2375`): a point light
 *  becomes `PointLight`, and one carrying spot angles becomes `SpotLight` with
 *  its outer angle. */
EPIX_EXPORT inline ClusterableObjectType point_or_spot_light_to_clusterable(const ExtractedPointLight& light) {
    if (light.spot_light_angles.has_value()) {
        return ClusterableObjectType{.kind = ClusterableObjectType::Kind::SpotLight,
                                     .shadows_enabled = light.shadows_enabled,
                                     .volumetric = light.volumetric,
                                     .outer_angle = light.spot_light_angles->second};
    }
    return ClusterableObjectType{
        .kind = ClusterableObjectType::Kind::PointLight, .shadows_enabled = light.shadows_enabled, .volumetric = light.volumetric};
}

/** @brief The key point lights are sorted by (Bevy `light.rs:860-865`):
 *  point-light-versus-spot-light and then shadows and volumetrics first, from the
 *  clusterable ordering, with the entity as a stable tiebreak.
 *
 *  Bevy's reasons, in its own order: the shader iterates point lights and spot
 *  lights as contiguous blocks, the sort puts the shadow-casting ones first so a
 *  light's index can select a shadow map, and the entity keys a consistent set
 *  when the light limit cuts the list. */
EPIX_EXPORT inline std::tuple<std::uint8_t, bool, bool, ecs::Entity> point_light_sort_key(
    ecs::Entity entity, const ExtractedPointLight& light) {
    const auto ordering = point_or_spot_light_to_clusterable(light).ordering();
    return {std::get<0>(ordering), std::get<1>(ordering), std::get<2>(ordering), entity};
}

/** @brief The key directional lights are sorted by (Bevy `light.rs:877-879`):
 *  volumetric first, then shadows, then the entity as a stable tiebreak.
 *
 *  The flags are Bevy's plain ascending booleans, so `false` sorts before `true`
 *  - which means lights with volumetric and shadows *disabled* come first, the
 *  opposite of what Bevy's comment above the sort describes. The code is what is
 *  ported, and the discrepancy is recorded rather than quietly "corrected",
 *  since the shadow-map indexing downstream is written against whichever order
 *  the sort actually produces. */
EPIX_EXPORT inline std::tuple<bool, bool, ecs::Entity> directional_light_sort_key(
    ecs::Entity entity, const ExtractedDirectionalLight& light) {
    return {light.volumetric, light.shadows_enabled, entity};
};

/** @brief A spot light's packed cone data (Bevy `prepare_lights`,
 *  `render/light.rs:918-933`): the light direction's x and z components, then the
 *  spot scale and offset that the shader uses to attenuate between the inner and
 *  outer angles.
 *
 *  The scale is `1 / max(cos(inner) - cos(outer), 1e-4)`, so the guard keeps it
 *  finite when the two angles coincide or the inner one is the wider - the cone
 *  the shader computes would otherwise divide by zero. The returned second value
 *  is `tan(outer)`, the tangent the shader needs for the cone test. */
EPIX_EXPORT inline std::pair<glm::vec4, float> spot_light_custom_data(float inner_angle, float outer_angle,
                                                                     glm::vec3 light_direction) {
    const float cos_outer = std::cos(outer_angle);
    const float spot_scale = 1.0f / std::max(std::cos(inner_angle) - cos_outer, 1e-4f);
    const float spot_offset = -cos_outer * spot_scale;
    return {glm::vec4(light_direction.x, light_direction.z, spot_scale, spot_offset), std::tan(outer_angle)};
}

/** @brief A point light's packed cube-face data (Bevy `render/light.rs:936-946`):
 *  the lower-right 2x2 values of the cube-face projection matrix, which the
 *  shader uses to reconstruct the direction to each of the six faces. */
EPIX_EXPORT inline glm::vec4 point_light_custom_data(const glm::mat4& cube_face_projection) {
    return glm::vec4(cube_face_projection[2][2], cube_face_projection[2][3], cube_face_projection[3][2],
                     cube_face_projection[3][3]);
}

/** @brief Whether a spot light's `SPOT_LIGHT_Y_NEGATIVE` flag is set (Bevy
 *  `render/light.rs:920-923`).
 *
 *  Bevy tests `is_sign_negative`, not `< 0.0`, so a direction of exactly `-0.0`
 *  counts as negative - which is what the shader expects, since it flips the cone
 *  around Y for these lights. */
EPIX_EXPORT inline bool spot_light_y_negative(glm::vec3 light_direction) {
    return std::signbit(light_direction.y);
}

/** @brief A light's colour premultiplied by its intensity, with the inverse
 *  square of the range in w (Bevy `render/light.rs:952-957`).
 *
 *  Bevy multiplies the whole `vec4` including alpha - "we don't use the alpha at
 *  all", so there is no reason to touch only the first three components - then
 *  takes the xyz and puts `1 / range²` in w. */
EPIX_EXPORT inline glm::vec4 pack_light_color_intensity_range(glm::vec4 color, float intensity, float range) {
    return glm::vec4(glm::vec3(color * intensity), 1.0f / (range * range));
}

}  // namespace epix::pbr
