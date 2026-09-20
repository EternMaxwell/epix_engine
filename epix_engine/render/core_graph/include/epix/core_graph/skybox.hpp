#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <optional>
#include <webgpu/webgpu.hpp>
#include <epix/common.hpp>
#endif
#include <epix/core_graph/core3d.hpp>

namespace epix::core_graph {

/** @brief Adds a skybox to a 3D camera, based on a cubemap texture (Bevy
 *  `Skybox`, `skybox/mod.rs:86-107`).
 *
 *  The image must produce a cube texture view; as in Bevy that is expressed with
 *  the image's own `texture_view_descriptor` rather than a dedicated image type. */
EPIX_EXPORT struct Skybox {
    /** @brief The cubemap to draw behind the scene. Defaults to the default
     *  image, as Bevy's `Handle::default()` does. */
    assets::Handle<image::Image> image{image::DEFAULT_IMAGE_HANDLE};
    /** @brief Scale factor applied to the image samples, in cd/m^2. */
    float brightness = 0.0f;
    /** @brief View-space rotation applied to the cubemap. */
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

/** @brief The per-view skybox uniforms (Bevy `SkyboxUniforms`).
 *
 *  `brightness` is already multiplied by the view's exposure and `transform` is
 *  the inverse of the component's rotation, as Bevy's extraction does
 *  (`skybox/mod.rs:114-134`). Bevy's `ShaderType` derives the layout for
 *  `{ f32, Mat4 }` with the matrix 16-byte aligned, so the three padding floats
 *  below reproduce what it inserts automatically: brightness at offset 0, the
 *  matrix at offset 16, 80 bytes in total. */
EPIX_EXPORT struct SkyboxUniforms {
    float brightness = 1.0f;
    /** @brief Padding that aligns the matrix to 16 bytes (Bevy's `ShaderType`
     *  inserts it implicitly). */
    std::array<float, 3> padding{};
    glm::mat4 transform{1.0f};
};

/** @brief Uniquely identifies a configuration of the skybox shader (Bevy
 *  `SkyboxPipelineKey`, `skybox/mod.rs:182-187`). */
EPIX_EXPORT struct SkyboxPipelineKey {
    /** @brief True when the view renders to a high dynamic range target. */
    bool hdr = false;
    /** @brief The view's MSAA sample count. */
    std::uint32_t samples = 1;
    /** @brief The depth format of the view's depth attachment. */
    wgpu::TextureFormat depth_format = wgpu::TextureFormat::eUndefined;

    [[nodiscard]] bool operator==(const SkyboxPipelineKey&) const noexcept;
};

/** @brief Draws a cubemap behind the scene (Bevy `SkyboxPipeline`).
 *
 *  Its single bind group holds the cube texture, a filtering sampler, the
 *  view uniform (dynamic) and the skybox uniforms. */
EPIX_EXPORT struct SkyboxPipeline {
    using Key = SkyboxPipelineKey;

    /** @brief The bind group layout shared by every specialized pipeline. */
    render::BindGroupLayoutDescriptor bind_group_layout;
    /** @brief A handle that identifies the compiled shader. */
    assets::Handle<shader::Shader> shader;

    /** @brief Creates the pipeline and its layout for `shader` (Bevy
     *  `SkyboxPipeline::new`, `skybox/mod.rs:156-175`). */
    static SkyboxPipeline create(assets::Handle<shader::Shader> shader);

    /** @brief Builds the render descriptor for `key` (Bevy
     *  `SpecializedRenderPipeline for SkyboxPipeline`, `:189-238`): writes no
     *  depth, compares with `GreaterEqual`, and targets the HDR or default
     *  colour format with no blending. */
    [[nodiscard]] render::RenderPipelineDescriptor specialize(Key key) const;
};

}  // namespace epix::core_graph

template <>
struct std::hash<epix::core_graph::SkyboxPipelineKey> {
    std::size_t operator()(const epix::core_graph::SkyboxPipelineKey& key) const noexcept;
};
