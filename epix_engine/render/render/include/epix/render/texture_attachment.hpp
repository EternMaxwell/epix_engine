#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <atomic>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <webgpu/webgpu.hpp>
#endif

#include <epix/render/render_resource.hpp>

namespace epix::render::render_resource {

/**
 * @brief A wrapper for a cached texture used as a render pass color
 * attachment (Bevy `ColorAttachment`). Clears with `clear_color` on first
 * use per frame, loads afterwards.
 */
struct ColorAttachment {
    /** @brief The cached texture rendered into. */
    CachedTexture texture;
    /** @brief Optional resolve target for multisampled attachments. */
    std::optional<CachedTexture> resolve_target;
    /** @brief Previous-frame texture (Bevy previous_frame_texture). */
    std::optional<CachedTexture> previous_frame_texture;
    /** @brief Optional clear color; when present the first use clears. */
    std::optional<glm::vec4> clear_color;
    /** @brief True until the first `get_attachment` call. */
    std::shared_ptr<std::atomic<bool>> is_first_call;

    ColorAttachment() : is_first_call(std::make_shared<std::atomic<bool>>(true)) {}

    ColorAttachment(CachedTexture texture,
                    std::optional<CachedTexture> resolve_target         = std::nullopt,
                    std::optional<CachedTexture> previous_frame_texture = std::nullopt,
                    std::optional<glm::vec4> clear_color                = std::nullopt)
        : texture(std::move(texture)),
          resolve_target(std::move(resolve_target)),
          previous_frame_texture(std::move(previous_frame_texture)),
          clear_color(clear_color),
          is_first_call(std::make_shared<std::atomic<bool>>(true)) {}

    /** @brief Build the wgpu color attachment (clears on first call when a
     * clear color is set; honors the resolve target like Bevy
     * texture_attachment.rs). */
    wgpu::RenderPassColorAttachment get_attachment() const {
        if (resolve_target) {
            const bool first_call = is_first_call->exchange(false);
            wgpu::LoadOp load_op  = (clear_color && first_call) ? wgpu::LoadOp::eClear : wgpu::LoadOp::eLoad;
            wgpu::RenderPassColorAttachment attachment;
            attachment.setView(resolve_target->default_view)
                .setResolveTarget(texture.default_view)
                .setDepthSlice(~0u)
                .setLoadOp(load_op)
                .setStoreOp(wgpu::StoreOp::eStore);
            if (clear_color && first_call) {
                attachment.setClearValue(wgpu::Color(clear_color->r, clear_color->g, clear_color->b, clear_color->a));
            }
            return attachment;
        }
        return get_unsampled_attachment();
    }

    /** @brief Build the attachment without a resolve target. */
    wgpu::RenderPassColorAttachment get_unsampled_attachment() const {
        const bool first_call = is_first_call->exchange(false);
        wgpu::LoadOp load_op  = (clear_color && first_call) ? wgpu::LoadOp::eClear : wgpu::LoadOp::eLoad;
        wgpu::RenderPassColorAttachment attachment;
        attachment.setView(texture.default_view)
            .setDepthSlice(~0u)
            .setLoadOp(load_op)
            .setStoreOp(wgpu::StoreOp::eStore);
        if (clear_color && first_call) {
            attachment.setClearValue(wgpu::Color(clear_color->r, clear_color->g, clear_color->b, clear_color->a));
        }
        return attachment;
    }

    /** @brief Mark as already used so the next get_attachment loads (Bevy
     * ColorAttachment::mark_as_cleared). */
    void mark_as_cleared() const noexcept { is_first_call->store(false, std::memory_order_seq_cst); }
};

/**
 * @brief A wrapper for a texture view used as a depth-only render pass
 * attachment (Bevy `DepthAttachment`).
 */
struct DepthAttachment {
    /** @brief Texture view used for depth testing/writing. */
    wgpu::TextureView view;
    /** @brief Optional clear value; present clears on first use. */
    std::optional<float> clear_value;
    /** @brief True until the first `get_attachment` call. */
    std::shared_ptr<std::atomic<bool>> is_first_call;

    explicit DepthAttachment(wgpu::TextureView view, std::optional<float> clear_value = std::nullopt)
        : view(std::move(view)),
          clear_value(clear_value),
          is_first_call(std::make_shared<std::atomic<bool>>(clear_value.has_value())) {}

    /** @brief Build the wgpu depth-stencil attachment. */
    wgpu::RenderPassDepthStencilAttachment get_attachment(wgpu::StoreOp store) const {
        const bool first_call = is_first_call->exchange(store != wgpu::StoreOp::eStore);
        wgpu::LoadOp load_op  = (clear_value && first_call) ? wgpu::LoadOp::eClear : wgpu::LoadOp::eLoad;
        return wgpu::RenderPassDepthStencilAttachment()
            .setView(view)
            .setDepthLoadOp(load_op)
            .setDepthStoreOp(store)
            .setDepthClearValue(clear_value.value_or(0.0f));
    }
};

/**
 * @brief The color attachment produced for a view target (Bevy
 * `OutputColorAttachment`).
 */
struct OutputColorAttachment {
    /** @brief The texture view rendered into. */
    wgpu::TextureView view;
    /** @brief Format of the output texture. */
    wgpu::TextureFormat format = wgpu::TextureFormat::eUndefined;
    /** @brief Optional resolve target view. */
    std::optional<wgpu::TextureView> resolve_target;
};

}  // namespace epix::render::render_resource
