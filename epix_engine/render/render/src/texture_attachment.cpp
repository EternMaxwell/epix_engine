
#include <epix/render/texture_attachment.hpp>

namespace epix::render::render_resource {

wgpu::RenderPassColorAttachment ColorAttachment::get_attachment() const {
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

}  // namespace epix::render::render_resource
