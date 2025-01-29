#include "epix/rdvk.h"

namespace epix::render::vulkan2 {
EPIX_API PassBase::SubpassInfo& PassBase::SubpassInfo::set_bind_point(
    vk::PipelineBindPoint bind_point
) {
    this->bind_point = bind_point;
    return *this;
}
EPIX_API PassBase::SubpassInfo& PassBase::SubpassInfo::set_colors(
    const vk::ArrayProxy<const vk::AttachmentReference>& attachments
) {
    color_attachments = std::vector<vk::AttachmentReference>(
        attachments.begin(), attachments.end()
    );
    return *this;
}
EPIX_API PassBase::SubpassInfo& PassBase::SubpassInfo::set_depth(
    vk::AttachmentReference attachment
) {
    depth_attachment = attachment;
    return *this;
}
EPIX_API PassBase::SubpassInfo& PassBase::SubpassInfo::set_resolves(
    const vk::ArrayProxy<const vk::AttachmentReference>& attachments
) {
    resolve_attachment = std::vector<vk::AttachmentReference>(
        attachments.begin(), attachments.end()
    );
    return *this;
}
EPIX_API PassBase::SubpassInfo& PassBase::SubpassInfo::set_inputs(
    const vk::ArrayProxy<const vk::AttachmentReference>& attachments
) {
    input_attachments = std::vector<vk::AttachmentReference>(
        attachments.begin(), attachments.end()
    );
    return *this;
}
EPIX_API PassBase::SubpassInfo& PassBase::SubpassInfo::set_preserves(
    const vk::ArrayProxy<const uint32_t>& attachments
) {
    preserve_attachments =
        std::vector<uint32_t>(attachments.begin(), attachments.end());
    return *this;
}

EPIX_API PassBase::PassBase(backend::Device& device) : _device(device) {}

EPIX_API void PassBase::create() {
    std::vector<vk::SubpassDescription> subpasses;
    subpasses.reserve(_subpasses.size());
    for (auto& subpass : _subpasses) {
        auto description = vk::SubpassDescription()
                               .setPipelineBindPoint(subpass.bind_point)
                               .setColorAttachments(subpass.color_attachments);
        if (subpass.depth_attachment) {
            description.setPDepthStencilAttachment(
                &subpass.depth_attachment.value()
            );
        }
        if (!subpass.resolve_attachment.empty()) {
            description.setResolveAttachments(subpass.resolve_attachment);
        }
        if (!subpass.input_attachments.empty()) {
            description.setInputAttachments(subpass.input_attachments);
        }
        if (!subpass.preserve_attachments.empty()) {
            description.setPreserveAttachments(subpass.preserve_attachments);
        }
        subpasses.push_back(description);
    }
    _render_pass =
        _device.createRenderPass(vk::RenderPassCreateInfo()
                                     .setAttachments(_attachments)
                                     .setSubpasses(subpasses)
                                     .setDependencies(_dependencies));
}

EPIX_API PassBase* PassBase::create_new(
    backend::Device& device, std::function<void(PassBase&)> pass_setup
) {
    auto pass = new PassBase(device);
    if (pass_setup) pass_setup(*pass);
    pass->create();
    return pass;
}
EPIX_API std::unique_ptr<PassBase> PassBase::create_unique(
    backend::Device& device, std::function<void(PassBase&)> pass_setup
) {
    std::unique_ptr<PassBase> pass(new PassBase(device));
    if (pass_setup) pass_setup(*pass);
    pass->create();
    return std::move(pass);
}
EPIX_API std::shared_ptr<PassBase> PassBase::create_shared(
    backend::Device& device, std::function<void(PassBase&)> pass_setup
) {
    std::shared_ptr<PassBase> pass(new PassBase(device));
    if (pass_setup) pass_setup(*pass);
    pass->create();
    return pass;
}
EPIX_API PassBase* PassBase::create_simple(backend::Device& device) {
    auto pass = new PassBase(device);
    pass->set_attachments(
        vk::AttachmentDescription()
            .setFormat(vk::Format::eB8G8R8A8Srgb)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setLoadOp(vk::AttachmentLoadOp::eLoad)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .setInitialLayout(vk::ImageLayout::eColorAttachmentOptimal)
            .setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal)
    );
    pass->subpass_info(0)
        .set_bind_point(vk::PipelineBindPoint::eGraphics)
        .set_colors(vk::AttachmentReference().setAttachment(0).setLayout(
            vk::ImageLayout::eColorAttachmentOptimal
        ));
    pass->create();
    return pass;
}
EPIX_API PassBase* PassBase::create_simple_depth(backend::Device& device) {
    auto pass                                          = new PassBase(device);
    std::vector<vk::AttachmentDescription> attachments = {
        vk::AttachmentDescription()
            .setFormat(vk::Format::eB8G8R8A8Srgb)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setLoadOp(vk::AttachmentLoadOp::eLoad)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .setInitialLayout(vk::ImageLayout::eColorAttachmentOptimal)
            .setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal),
        vk::AttachmentDescription()
            .setFormat(vk::Format::eD32Sfloat)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setLoadOp(vk::AttachmentLoadOp::eLoad)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .setInitialLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
            .setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
    };
    pass->set_attachments(attachments);
    pass->subpass_info(0)
        .set_bind_point(vk::PipelineBindPoint::eGraphics)
        .set_colors(vk::AttachmentReference().setAttachment(0).setLayout(
            vk::ImageLayout::eColorAttachmentOptimal
        ))
        .set_depth(vk::AttachmentReference().setAttachment(1).setLayout(
            vk::ImageLayout::eDepthStencilAttachmentOptimal
        ));
    pass->create();
    return pass;
}

EPIX_API void PassBase::set_attachments(
    const vk::ArrayProxy<const vk::AttachmentDescription>& attachments
) {
    _attachments = std::vector<vk::AttachmentDescription>(
        attachments.begin(), attachments.end()
    );
}
EPIX_API void PassBase::set_dependencies(
    const vk::ArrayProxy<const vk::SubpassDependency>& dependencies
) {
    _dependencies = std::vector<vk::SubpassDependency>(
        dependencies.begin(), dependencies.end()
    );
}

EPIX_API PassBase::SubpassInfo& PassBase::subpass_info(uint32_t index) {
    if (index >= _subpasses.size()) {
        _subpasses.resize(index + 1);
    }
    return _subpasses[index];
}

EPIX_API uint32_t PassBase::add_pipeline(
    uint32_t subpass,
    const std::string& name,
    std::function<void(PipelineBase&)> pipeline_setup
) {
    if (subpass >= _pipelines.size()) {
        _pipelines.resize(subpass + 1);
    }
    auto pipeline = std::make_unique<PipelineBase>();
    pipeline_setup(*pipeline);
    pipeline->device        = _device;
    pipeline->render_pass   = _render_pass;
    pipeline->subpass_index = subpass;
    pipeline->create();
    _pipelines[subpass].emplace_back(std::move(pipeline));
    _pipeline_maps[subpass].emplace(name, _pipelines[subpass].size() - 1);
    return _pipelines[subpass].size() - 1;
}

EPIX_API uint32_t PassBase::add_pipeline(
    uint32_t subpass, const std::string& name, PipelineBase* pipeline
) {
    if (subpass >= subpass_count()) {
        spdlog::warn("PassBase::add_pipeline called with invalid subpass");
        return -1;
    }
    if (subpass >= _pipelines.size()) {
        _pipelines.resize(subpass + 1);
        _pipeline_maps.resize(subpass + 1);
    }
    pipeline->device        = _device;
    pipeline->render_pass   = _render_pass;
    pipeline->subpass_index = subpass;
    pipeline->create();
    _pipelines[subpass].emplace_back(pipeline);
    _pipeline_maps[subpass].emplace(name, _pipelines[subpass].size() - 1);
    return _pipelines[subpass].size() - 1;
}

EPIX_API uint32_t
PassBase::pipeline_index(uint32_t subpass, const std::string& name) const {
    if (subpass >= _pipeline_maps.size()) {
        spdlog::warn("PassBase::pipeline_index called with invalid subpass");
        return -1;
    }
    auto it = _pipeline_maps[subpass].find(name);
    if (it == _pipeline_maps[subpass].end()) {
        spdlog::warn("PassBase::pipeline_index called with invalid name");
        return -1;
    }
    return it->second;
}

EPIX_API PipelineBase* PassBase::get_pipeline(uint32_t subpass, uint32_t index)
    const {
    if (subpass >= _pipelines.size()) {
        spdlog::warn("PassBase::get_pipeline called with invalid subpass");
        return nullptr;
    }
    if (index >= _pipelines[subpass].size()) {
        spdlog::warn("PassBase::get_pipeline called with invalid index");
        return nullptr;
    }
    return _pipelines[subpass][index].get();
}

EPIX_API PipelineBase* PassBase::get_pipeline(
    uint32_t subpass, const std::string& name
) const {
    if (subpass >= _pipeline_maps.size()) {
        spdlog::warn("PassBase::get_pipeline called with invalid subpass");
        return nullptr;
    }
    auto it = _pipeline_maps[subpass].find(name);
    if (it == _pipeline_maps[subpass].end()) {
        spdlog::warn("PassBase::get_pipeline called with invalid name");
        return nullptr;
    }
    return get_pipeline(subpass, it->second);
}

EPIX_API void PassBase::destroy() {
    for (auto& subpasses : _pipelines) {
        for (auto& pipeline : subpasses) {
            pipeline->destroy();
        }
    }
    _device.destroyRenderPass(_render_pass);
}

EPIX_API uint32_t PassBase::subpass_count() const { return _subpasses.size(); }
}  // namespace epix::render::vulkan2