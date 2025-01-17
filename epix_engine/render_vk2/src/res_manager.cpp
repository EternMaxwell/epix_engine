#include "epix/rdvk_res.h"

namespace epix::render::vulkan2 {
using Device               = backend::Device;
using Buffer               = backend::Buffer;
using Image                = backend::Image;
using ImageView            = backend::ImageView;
using Sampler              = backend::Sampler;
using AllocationCreateInfo = backend::AllocationCreateInfo;
EPIX_API VulkanResources::VulkanResources(Device device) {
    resources = new VulkanResources_T{device};
    std::vector<vk::DescriptorPoolSize> pool_sizes = {
        vk::DescriptorPoolSize()
            .setType(vk::DescriptorType::eSampledImage)
            .setDescriptorCount(65536),
        vk::DescriptorPoolSize()
            .setType(vk::DescriptorType::eSampler)
            .setDescriptorCount(65536),
    };
    vk::DescriptorPoolCreateInfo pool_info;
    pool_info.setPoolSizes(pool_sizes);
    pool_info.setMaxSets(1);
    pool_info.setFlags(
        vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet |
        vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind
    );
    resources->descriptor_pool = device.createDescriptorPool(pool_info);
    std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        vk::DescriptorSetLayoutBinding()
            .setBinding(0)
            .setDescriptorType(vk::DescriptorType::eSampledImage)
            .setDescriptorCount(65536)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding()
            .setBinding(1)
            .setDescriptorType(vk::DescriptorType::eSampler)
            .setDescriptorCount(65536)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment),
    };
    vk::DescriptorSetLayoutCreateInfo layout_info;
    layout_info.setBindings(bindings);
    resources->descriptor_set_layout =
        device.createDescriptorSetLayout(layout_info);
    vk::DescriptorSetAllocateInfo alloc_info;
    alloc_info.setDescriptorPool(resources->descriptor_pool);
    alloc_info.setSetLayouts(resources->descriptor_set_layout);
    resources->descriptor_set = device.allocateDescriptorSets(alloc_info)[0];
}
EPIX_API Device& VulkanResources::device() const { return resources->device; }
EPIX_API void VulkanResources::destroy() {
    auto& device = resources->device;
    device.destroyDescriptorSetLayout(resources->descriptor_set_layout);
    device.destroyDescriptorPool(resources->descriptor_pool);
    for (auto& buffer : resources->buffers) {
        buffer.destroy();
    }
    for (auto& image : resources->images) {
        image.destroy();
    }
    for (auto& image_view : resources->image_views) {
        device.destroyImageView(image_view);
    }
    for (auto& sampler : resources->samplers) {
        device.destroySampler(sampler);
    }
    delete resources;
}
EPIX_API uint32_t
VulkanResources::add_buffer(const std::string& name, Buffer buffer) {
    auto& buffer_map          = resources->buffer_map;
    auto& buffers             = resources->buffers;
    auto& buffer_names        = resources->buffer_names;
    auto& buffer_free_indices = resources->buffer_free_indices;
    if (buffer_map.contains(name)) {
        return buffer_map[name];
    }
    if (!buffer_free_indices.empty()) {
        auto index = buffer_free_indices.top();
        buffer_free_indices.pop();
        buffer_map[name] = index;
        buffers[index]   = buffer;
        return index;
    }
    buffer_names.push_back(name);
    buffer_map[name] = buffers.size();
    buffers.push_back(buffer);
    return buffers.size() - 1;
}
EPIX_API uint32_t
VulkanResources::add_image(const std::string& name, Image image) {
    auto& image_map          = resources->image_map;
    auto& images             = resources->images;
    auto& image_names        = resources->image_names;
    auto& image_free_indices = resources->image_free_indices;
    if (image_map.contains(name)) {
        return image_map[name];
    }
    if (!image_free_indices.empty()) {
        auto index = image_free_indices.top();
        image_free_indices.pop();
        image_map[name] = index;
        images[index]   = image;
        return index;
    }
    image_names.push_back(name);
    image_map[name] = images.size();
    images.push_back(image);
    return images.size() - 1;
}
EPIX_API uint32_t
VulkanResources::add_image_view(const std::string& name, ImageView image_view) {
    auto& image_view_map    = resources->image_view_map;
    auto& image_views       = resources->image_views;
    auto& image_view_names  = resources->image_view_names;
    auto& view_cache        = resources->view_cache;
    auto& view_free_indices = resources->view_free_indices;
    if (image_view_map.contains(name)) {
        return image_view_map[name];
    }
    if (!view_free_indices.empty()) {
        auto index = view_free_indices.top();
        view_free_indices.pop();
        image_view_map[name] = index;
        image_views[index]   = image_view;
        return index;
    }
    image_view_names.push_back(name);
    image_view_map[name] = image_views.size();
    image_views.push_back(image_view);
    view_cache.emplace_back((uint32_t)image_views.size() - 1, image_view);
    return image_views.size() - 1;
}
EPIX_API uint32_t
VulkanResources::add_sampler(const std::string& name, Sampler sampler) {
    auto& sampler_map          = resources->sampler_map;
    auto& samplers             = resources->samplers;
    auto& sampler_names        = resources->sampler_names;
    auto& sampler_cache        = resources->sampler_cache;
    auto& sampler_free_indices = resources->sampler_free_indices;
    if (sampler_map.contains(name)) {
        return sampler_map[name];
    }
    if (!sampler_free_indices.empty()) {
        auto index = sampler_free_indices.top();
        sampler_free_indices.pop();
        sampler_map[name] = index;
        samplers[index]   = sampler;
        return index;
    }
    sampler_names.push_back(name);
    sampler_map[name] = samplers.size();
    samplers.push_back(sampler);
    sampler_cache.emplace_back((uint32_t)samplers.size() - 1, sampler);
    return samplers.size() - 1;
}
EPIX_API void VulkanResources::apply_cache() {
    auto& device               = resources->device;
    auto& buffers              = resources->buffers;
    auto& buffer_free_indices  = resources->buffer_free_indices;
    auto& buffer_cache_remove  = resources->buffer_cache_remove;
    auto& images               = resources->images;
    auto& image_free_indices   = resources->image_free_indices;
    auto& image_cache_remove   = resources->image_cache_remove;
    auto& image_views          = resources->image_views;
    auto& view_free_indices    = resources->view_free_indices;
    auto& view_cache           = resources->view_cache;
    auto& view_cache_remove    = resources->view_cache_remove;
    auto& samplers             = resources->samplers;
    auto& sampler_free_indices = resources->sampler_free_indices;
    auto& sampler_cache        = resources->sampler_cache;
    auto& sampler_cache_remove = resources->sampler_cache_remove;
    auto& descriptor_set       = resources->descriptor_set;

    for (auto index : buffer_cache_remove) {
        device.destroyBuffer(buffers[index]);
        buffer_free_indices.push(index);
    }
    for (auto index : image_cache_remove) {
        device.destroyImage(images[index]);
        image_free_indices.push(index);
    }
    std::vector<vk::DescriptorImageInfo> image_infos;
    image_infos.reserve(view_cache.size() + sampler_cache.size());
    std::vector<vk::WriteDescriptorSet> descriptor_writes;
    descriptor_writes.reserve(view_cache.size() + sampler_cache.size());
    for (auto&& [index, view] : view_cache) {
        image_infos.push_back(
            vk::DescriptorImageInfo().setImageView(view).setImageLayout(
                vk::ImageLayout::eShaderReadOnlyOptimal
            )
        );
        descriptor_writes.push_back(
            vk::WriteDescriptorSet()
                .setDstSet(descriptor_set)
                .setDstBinding(0)
                .setDstArrayElement(index)
                .setDescriptorType(vk::DescriptorType::eSampledImage)
                .setDescriptorCount(1)
                .setImageInfo(image_infos.back())
        );
    }
    for (auto&& [index, sampler] : sampler_cache) {
        image_infos.push_back(vk::DescriptorImageInfo().setSampler(sampler));
        descriptor_writes.push_back(
            vk::WriteDescriptorSet()
                .setDstSet(descriptor_set)
                .setDstBinding(1)
                .setDstArrayElement(index)
                .setDescriptorType(vk::DescriptorType::eSampler)
                .setDescriptorCount(1)
                .setImageInfo(image_infos.back())
        );
    }
    device.updateDescriptorSets(descriptor_writes, {});
    image_infos.clear();
    descriptor_writes.clear();
    for (auto index : view_cache_remove) {
        device.destroyImageView(image_views[index]);
        view_free_indices.push(index);
    }
    for (auto index : sampler_cache_remove) {
        device.destroySampler(samplers[index]);
        sampler_free_indices.push(index);
    }
    view_cache.clear();
    sampler_cache.clear();
    buffer_cache_remove.clear();
    image_cache_remove.clear();
    view_cache_remove.clear();
    sampler_cache_remove.clear();
}
EPIX_API Buffer VulkanResources::get_buffer(const std::string& name) const {
    auto& buffers = resources->buffers;
    auto index    = buffer_index(name);
    if (index == -1) {
        return Buffer();
    }
    return buffers[index];
}
EPIX_API Image VulkanResources::get_image(const std::string& name) const {
    auto& images = resources->images;
    auto index   = image_index(name);
    if (index == -1) {
        return Image();
    }
    return images[index];
}
EPIX_API ImageView VulkanResources::get_image_view(const std::string& name
) const {
    auto& image_views = resources->image_views;
    auto index        = image_view_index(name);
    if (index == -1) {
        return ImageView();
    }
    return image_views[index];
}
EPIX_API Sampler VulkanResources::get_sampler(const std::string& name) const {
    auto& samplers = resources->samplers;
    auto index     = sampler_index(name);
    if (index == -1) {
        return Sampler();
    }
    return samplers[index];
}
EPIX_API Buffer VulkanResources::get_buffer(uint32_t index) const {
    return resources->buffers[index];
}
EPIX_API Image VulkanResources::get_image(uint32_t index) const {
    return resources->images[index];
}
EPIX_API ImageView VulkanResources::get_image_view(uint32_t index) const {
    return resources->image_views[index];
}
EPIX_API Sampler VulkanResources::get_sampler(uint32_t index) const {
    return resources->samplers[index];
}
EPIX_API void VulkanResources::remove_buffer(const std::string& name) {
    auto& buffer_cache_remove = resources->buffer_cache_remove;
    auto& buffer_map          = resources->buffer_map;
    if (!buffer_map.contains(name)) {
        return;
    }
    auto index = buffer_map[name];
    buffer_cache_remove.push_back(index);
}
EPIX_API void VulkanResources::remove_image(const std::string& name) {
    auto& image_cache_remove = resources->image_cache_remove;
    auto& image_map          = resources->image_map;
    if (!image_map.contains(name)) {
        return;
    }
    auto index = image_map[name];
    image_cache_remove.push_back(index);
}
EPIX_API void VulkanResources::remove_image_view(const std::string& name) {
    auto& view_cache_remove = resources->view_cache_remove;
    auto& image_view_map    = resources->image_view_map;
    if (!image_view_map.contains(name)) {
        return;
    }
    auto index = image_view_map[name];
    view_cache_remove.push_back(index);
}
EPIX_API void VulkanResources::remove_sampler(const std::string& name) {
    auto& sampler_cache_remove = resources->sampler_cache_remove;
    auto& sampler_map          = resources->sampler_map;
    if (!sampler_map.contains(name)) {
        return;
    }
    auto index = sampler_map[name];
    sampler_cache_remove.push_back(index);
}
EPIX_API void VulkanResources::remove_buffer(uint32_t index) {
    resources->buffer_cache_remove.push_back(index);
}
EPIX_API void VulkanResources::remove_image(uint32_t index) {
    resources->image_cache_remove.push_back(index);
}
EPIX_API void VulkanResources::remove_image_view(uint32_t index) {
    resources->view_cache_remove.push_back(index);
}
EPIX_API void VulkanResources::remove_sampler(uint32_t index) {
    resources->sampler_cache_remove.push_back(index);
}
EPIX_API uint32_t VulkanResources::buffer_index(const std::string& name) const {
    auto& buffer_map = resources->buffer_map;
    if (!buffer_map.contains(name)) {
        return -1;
    }
    return buffer_map.at(name);
}
EPIX_API uint32_t VulkanResources::image_index(const std::string& name) const {
    auto& image_map = resources->image_map;
    if (!image_map.contains(name)) {
        return -1;
    }
    return image_map.at(name);
}
EPIX_API uint32_t VulkanResources::image_view_index(const std::string& name
) const {
    auto& image_view_map = resources->image_view_map;
    if (!image_view_map.contains(name)) {
        return -1;
    }
    return image_view_map.at(name);
}
EPIX_API uint32_t VulkanResources::sampler_index(const std::string& name
) const {
    auto& sampler_map = resources->sampler_map;
    if (!sampler_map.contains(name)) {
        return -1;
    }
    return sampler_map.at(name);
}
EPIX_API vk::DescriptorSet VulkanResources::get_descriptor_set() const {
    return resources->descriptor_set;
}
EPIX_API vk::DescriptorSetLayout VulkanResources::get_descriptor_set_layout(
) const {
    return resources->descriptor_set_layout;
}
EPIX_API void VkResourcePlugin::build(epix::App& app) {
    app.add_system(epix::PreStartup, systems::create_res_manager)
        .after(vulkan2::systems::create_context);
    app.add_system(epix::PreExtract, systems::extract_res_manager);
    app.add_system(epix::PostExit, systems::destroy_res_manager)
        .before(vulkan2::systems::destroy_context);
}
}  // namespace epix::render::vulkan2