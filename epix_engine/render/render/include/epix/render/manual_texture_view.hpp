#pragma once

#include <epix/camera.hpp>
#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/ecs.hpp>
#include <glm/glm.hpp>
#include <unordered_map>
#include <utility>
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
    /** @brief Size of the texture in pixels. */
    glm::uvec2 size = glm::uvec2(0, 0);
    /** @brief Format of the view. */
    wgpu::TextureFormat view_format = wgpu::TextureFormat::eRGBA8Unorm;

    /** @brief Construct a view using Bevy's default sRGB texture format. */
    [[nodiscard]] static ManualTextureView with_default_format(wgpu::TextureView texture_view,
                                                                glm::uvec2 size) noexcept {
        return ManualTextureView{
            .texture_view = std::move(texture_view),
            .size         = size,
            .view_format  = wgpu::TextureFormat::eRGBA8UnormSrgb,
        };
    }
};

/**
 * @brief Resource storing manually managed texture views keyed by handle
 * (Bevy ManualTextureViews).
 */
struct ManualTextureViews : std::unordered_map<::epix::camera::ManualTextureViewHandle, ManualTextureView> {
    using std::unordered_map<::epix::camera::ManualTextureViewHandle, ManualTextureView>::unordered_map;
};

}  // namespace epix::render::texture
