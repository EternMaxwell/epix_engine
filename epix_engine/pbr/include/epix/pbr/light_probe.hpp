#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <glm/glm.hpp>
#include <epix/common.hpp>
#endif
#include <epix/pbr/probe.hpp>

namespace epix::pbr {

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
struct ShaderTypeInfo<::epix::pbr::EnvironmentMapUniform>
    : RawShaderType<::epix::pbr::EnvironmentMapUniform> {};
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

}  // namespace epix::pbr
