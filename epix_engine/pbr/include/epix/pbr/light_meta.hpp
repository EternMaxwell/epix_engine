#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <string>
#include <vector>
#include <webgpu/webgpu.hpp>
#include <epix/common.hpp>
#endif
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

}  // namespace epix::pbr
