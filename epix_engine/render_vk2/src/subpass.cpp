#include "epix/rdvk.h"

namespace epix::render::vulkan2 {
EPIX_API void Subpass::activate_pipeline(
    uint32_t index,
    std::function<void(std::vector<vk::Viewport>&, std::vector<vk::Rect2D>&)>
        func_viewport_scissor,
    std::function<void(backend::Device&, std::vector<backend::DescriptorSet>&)>
        func_desc
) {
    _active_pipeline = -1;
    if (index >= _pipelines.size()) {
        spdlog::warn("Subpass::activate_pipeline called with invalid index");
        return;
    }
    if (func_desc) {
        func_desc(_device, _descriptor_sets[index]);
    }
    if (func_viewport_scissor) {
        func_viewport_scissor(_viewports[index], _scissors[index]);
    }
    _cmd.bindPipeline(
        vk::PipelineBindPoint::eGraphics, _pipelines[index]->pipeline
    );
    if (_pipelines[index]->descriptor_set_layouts.size() >
        _descriptor_sets[index].size()) {
        spdlog::warn(
            "Subpass::activate_pipeline: Descriptor sets (size {}) provided is "
            "less than needed ({}). Ignoring call.",
            _descriptor_sets[index].size(),
            _pipelines[index]->descriptor_set_layouts.size()
        );
        return;
    }
    _cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, _pipelines[index]->pipeline_layout, 0,
        _descriptor_sets[index], {}
    );
    _cmd.setViewport(0, _viewports[index]);
    _cmd.setScissor(0, _scissors[index]);
    _active_pipeline = index;
}

EPIX_API uint32_t Subpass::add_pipeline(
    const PipelineBase* pipeline,
    std::function<std::vector<
        backend::
            DescriptorSet>(backend::Device&, const backend::DescriptorPool&, const std::vector<backend::DescriptorSetLayout>&)>
        func_desc_set,
    std::function<
        void(backend::Device&, const backend::DescriptorPool&, std::vector<backend::DescriptorSet>&)>
        func_destroy_desc_set
) {
    _pipelines.push_back(pipeline);
    if (func_desc_set) {
        _descriptor_sets.push_back(func_desc_set(
            _device, pipeline->descriptor_pool, pipeline->descriptor_set_layouts
        ));
    } else if (pipeline->descriptor_pool) {
        _descriptor_sets.push_back(_device.allocateDescriptorSets(
            vk::DescriptorSetAllocateInfo()
                .setDescriptorPool(pipeline->descriptor_pool)
                .setSetLayouts(pipeline->descriptor_set_layouts)
        ));
    }
    if (_descriptor_sets.size() < _pipelines.size()) {
        _descriptor_sets.resize(_pipelines.size());
    }
    _funcs_destroy_desc_set.push_back(func_destroy_desc_set);
    _viewports.resize(_pipelines.size());
    _scissors.resize(_pipelines.size());
    return _pipelines.size() - 1;
}

EPIX_API Subpass::Subpass(Pass& pass) : _device(pass._device) {}

EPIX_API void Subpass::destroy() {
    for (size_t i = 0; i < _pipelines.size(); i++) {
        if (_funcs_destroy_desc_set[i]) {
            _funcs_destroy_desc_set[i](
                _device, _pipelines[i]->descriptor_pool, _descriptor_sets[i]
            );
        }
    }
}

EPIX_API void Subpass::begin(backend::CommandBuffer& cmd) { _cmd = cmd; }

EPIX_API void Subpass::task(std::function<void(backend::CommandBuffer&)> func) {
    func(_cmd);
}
}  // namespace epix::render::vulkan2