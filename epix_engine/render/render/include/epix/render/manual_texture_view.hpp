#pragma once

#include <epix/common.hpp>
#include <epix/camera.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/ecs.hpp>
#include <glm/glm.hpp>
#include <unordered_map>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::render::texture {
/**
 * @brief A manually managed texture view for use as a render target (Bevy
 * ManualTextureView).
 */
struct ManualTextureView {
    /** @brief The texture view to render into. */
    wgpu::TextureView texture_view;
    /** @brief Optional backing texture for direct-wgpu consumers such as
     * screenshot readback.  A view alone is sufficient for rendering, but
     * WebGPU copies require the source texture. */
    wgpu::Texture texture;
    /** @brief Size of the texture in pixels. */
    glm::uvec2 size = glm::uvec2(0, 0);
    /** @brief Format of the view. */
    wgpu::TextureFormat view_format = wgpu::TextureFormat::eRGBA8Unorm;
};

/**
 * @brief Resource storing manually managed texture views keyed by handle
 * (Bevy ManualTextureViews).
 */
struct ManualTextureViews {
    /** @brief Stored views. */
    std::unordered_map<::epix::camera::ManualTextureViewHandle, ManualTextureView> views;
};

}  // namespace epix::render::texture
