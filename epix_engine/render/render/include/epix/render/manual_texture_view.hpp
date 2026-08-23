#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <atomic>
#include <cstdint>
#include <epix/ecs.hpp>
#include <functional>
#include <glm/glm.hpp>
#include <unordered_map>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::render::texture {
/**
 * @brief Handle identifying a manual texture view (Bevy
 * bevy_camera::ManualTextureViewHandle).
 */
struct ManualTextureViewHandle {
    /** @brief Unique handle value. */
    std::uint32_t id = 0;

    constexpr ManualTextureViewHandle() noexcept = default;
    explicit constexpr ManualTextureViewHandle(std::uint32_t value) noexcept : id(value) {}
    static ManualTextureViewHandle create() noexcept {
        static std::atomic<std::uint32_t> counter{0};
        return ManualTextureViewHandle{counter.fetch_add(1, std::memory_order_relaxed)};
    }
    bool operator==(const ManualTextureViewHandle&) const = default;
};

}  // namespace epix::render::texture

template <>
struct std::hash<epix::render::texture::ManualTextureViewHandle> {
    std::size_t operator()(const epix::render::texture::ManualTextureViewHandle& h) const noexcept {
        return std::hash<std::uint32_t>{}(h.id);
    }
};

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
};

/**
 * @brief Resource storing manually managed texture views keyed by handle
 * (Bevy ManualTextureViews).
 */
struct ManualTextureViews {
    /** @brief Stored views. */
    std::unordered_map<ManualTextureViewHandle, ManualTextureView> views;
};

}  // namespace epix::render::texture
