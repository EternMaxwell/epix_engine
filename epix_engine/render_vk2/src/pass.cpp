#include "epix/rdvk.h"

namespace epix::render::vulkan2 {
EPIX_API Pass::Pass(
    PassBase* base,
    backend::CommandPool& command_pool,
    std::function<void(Pass&, PassBase&)> subpass_setup
)
    : _base(base),
      _device(base->_device),
      _cmd_pool(command_pool),
      _render_pass(base->_render_pass),
      _subpass_count(base->subpass_count()) {
    _subpasses.resize(_subpass_count);
    _cmd = _device.allocateCommandBuffers(
        vk::CommandBufferAllocateInfo()
            .setCommandPool(_cmd_pool)
            .setCommandBufferCount(1)
            .setLevel(vk::CommandBufferLevel::ePrimary)
    )[0];
    _fence = _device.createFence(
        vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled)
    );
    subpass_setup(*this, *base);
    _base = nullptr;  // For thread safety, base should only be used in
                      // constructor
}

EPIX_API Pass& Pass::add_subpass(
    uint32_t index,
    std::function<void(PassBase&, Pass&, Subpass&)> subpass_setup
) {
    if (!_base) {
        spdlog::error("Pass::add_subpass called outside of constructor.");
        throw std::runtime_error(
            "Pass::add_subpass called outside of constructor."
        );
    }
    if (index >= _subpass_count) {
        spdlog::warn("Pass::add_subpass: Subpass count exceeded. Ignoring call."
        );
        return *this;
    }
    _subpasses[index] = Subpass(*this);
    if (subpass_setup) {
        subpass_setup(*_base, *this, _subpasses[index]);
    }
    return *this;
}

EPIX_API uint32_t Pass::subpass_add_pipeline(
    uint32_t subpass_index,
    uint32_t pipeline_index,
    std::function<std::vector<
        backend::
            DescriptorSet>(backend::Device&, const backend::DescriptorPool&, const std::vector<backend::DescriptorSetLayout>&)>
        func_desc_set,
    std::function<
        void(backend::Device&, const backend::DescriptorPool&, std::vector<backend::DescriptorSet>&)>
        func_destroy_desc_set
) {
    if (!_base) {
        spdlog::error(
            "Pass::subpass_add_pipeline called outside of constructor."
        );
        throw std::runtime_error(
            "Pass::subpass_add_pipeline called outside of constructor."
        );
    }
    if (subpass_index >= _subpass_count) {
        spdlog::warn(
            "Pass::subpass_add_pipeline: Subpass count exceeded. Ignoring "
            "call."
        );
        return -1;
    }
    if (pipeline_index >= _base->_pipelines[subpass_index].size()) {
        spdlog::warn(
            "Pass::subpass_add_pipeline: Pipeline count exceeded. Ignoring "
            "call."
        );
        return -1;
    }
    return _subpasses[subpass_index].add_pipeline(
        _base->_pipelines[subpass_index][pipeline_index].get(), func_desc_set,
        func_destroy_desc_set
    );
}

EPIX_API uint32_t Pass::subpass_add_pipeline(
    uint32_t subpass_index,
    const std::string& pipeline_name,
    std::function<std::vector<
        backend::
            DescriptorSet>(backend::Device&, const backend::DescriptorPool&, const std::vector<backend::DescriptorSetLayout>&)>
        func_desc_set,
    std::function<
        void(backend::Device&, const backend::DescriptorPool&, std::vector<backend::DescriptorSet>&)>
        func_destroy_desc_set
) {
    if (!_base) {
        spdlog::error(
            "Pass::subpass_add_pipeline called outside of constructor."
        );
        throw std::runtime_error(
            "Pass::subpass_add_pipeline called outside of constructor."
        );
    }
    if (subpass_index >= _subpass_count) {
        spdlog::warn(
            "Pass::subpass_add_pipeline: Subpass count exceeded. Ignoring "
            "call."
        );
        return -1;
    }
    uint32_t pipeline_index =
        _base->pipeline_index(subpass_index, pipeline_name);
    if (pipeline_index == -1) {
        spdlog::warn(
            "Pass::subpass_add_pipeline: Pipeline not found. Ignoring call."
        );
        return -1;
    }
    return subpass_add_pipeline(
        subpass_index, pipeline_index, func_desc_set, func_destroy_desc_set
    );
}

EPIX_API void Pass::begin(
    std::function<backend::Framebuffer(backend::Device&, backend::RenderPass&)>
        func,
    vk::Extent2D extent
) {
    if (recording) {
        spdlog::warn("Pass::begin called while already recording.");
        return;
    }
    if (ready) {
        spdlog::warn(
            "Pass::begin called while last command buffer is not "
            "submitted."
        );
        return;
    }
    _extent = extent;
    _device.waitForFences(_fence, true, UINT64_MAX);
    _cmd.reset(vk::CommandBufferResetFlagBits::eReleaseResources);
    if (_framebuffer) {
        _device.destroyFramebuffer(_framebuffer);
    }
    _framebuffer = func(_device, _render_pass);
    _cmd.begin(vk::CommandBufferBeginInfo());
    recording = true;
}

EPIX_API Subpass& Pass::next_subpass() {
    if (!recording) {
        spdlog::warn("Pass::subpass called outside of recording context.");
        return _subpasses[0];
    }
    if (!in_render_pass) {
        _cmd.beginRenderPass(
            vk::RenderPassBeginInfo()
                .setRenderPass(_render_pass)
                .setFramebuffer(_framebuffer)
                .setRenderArea(vk::Rect2D().setExtent(_extent)),
            vk::SubpassContents::eInline
        );
        auto& subpass = _subpasses[0];
        subpass.begin(_cmd);
        in_render_pass  = true;
        current_subpass = 0;
        return subpass;
    }
    current_subpass++;
    if (current_subpass >= _subpass_count) {
        spdlog::warn(
            "Pass::subpass called after last subpass. Returning "
            "first subpass."
        );
        return _subpasses[0];
    }
    _cmd.nextSubpass(vk::SubpassContents::eInline);
    auto& subpass = _subpasses[current_subpass];
    subpass.begin(_cmd);
    return subpass;
}

EPIX_API void Pass::end() {
    if (!recording) {
        spdlog::warn("Pass::end called outside of recording context.");
        return;
    }
    if (!in_render_pass) {
        spdlog::warn("Pass::end called outside of render pass.");
        return;
    }
    _cmd.endRenderPass();
    _cmd.end();
    recording      = false;
    in_render_pass = false;
    ready          = true;
}

EPIX_API void Pass::submit(backend::Queue& queue) {
    if (!ready) {
        spdlog::warn("Pass::submit called while command buffer is not ready.");
        return;
    }
    _device.resetFences(_fence);
    queue.submit(vk::SubmitInfo().setCommandBuffers(_cmd), _fence);
}
}  // namespace epix::render::vulkan2