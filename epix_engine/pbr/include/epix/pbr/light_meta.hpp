#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <webgpu/webgpu.hpp>
#include <epix/common.hpp>
#endif
#include <epix/camera/visibility.hpp>
#include <epix/pbr/cascade.hpp>
#include <epix/pbr/gpu_lights.hpp>

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

}  // namespace epix::pbr
