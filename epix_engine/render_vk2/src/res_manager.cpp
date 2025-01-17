#include "epix/rdvk.h"

namespace epix::render::vulkan2 {
using Device               = backend::Device;
using Buffer               = backend::Buffer;
using Image                = backend::Image;
using ImageView            = backend::ImageView;
using Sampler              = backend::Sampler;
using AllocationCreateInfo = backend::AllocationCreateInfo;
EPIX_API VulkanResources::VulkanResources(Device device) : m_device(device) {
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
    descriptor_pool = device.createDescriptorPool(pool_info);
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
    descriptor_set_layout = device.createDescriptorSetLayout(layout_info);
    vk::DescriptorSetAllocateInfo alloc_info;
    alloc_info.setDescriptorPool(descriptor_pool);
    alloc_info.setSetLayouts(descriptor_set_layout);
    descriptor_set = device.allocateDescriptorSets(alloc_info)[0];
}
EPIX_API Device& VulkanResources::device() const { return m_device; }
EPIX_API void VulkanResources::destroy() {
    m_device.destroyDescriptorSetLayout(descriptor_set_layout);
    m_device.destroyDescriptorPool(descriptor_pool);
    for (auto& buffer : buffers) {
        buffer.destroy();
    }
    for (auto& image : images) {
        image.destroy();
    }
    for (auto& image_view : image_views) {
        m_device.destroyImageView(image_view);
    }
    for (auto& sampler : samplers) {
        m_device.destroySampler(sampler);
    }
}
EPIX_API uint32_t
VulkanResources::add_buffer(const std::string& name, Buffer buffer) {
    if (buffer_map.contains(name)) {
        return buffer_map.at(name);
    }
    if (!buffer_free_indices.empty()) {
        auto index = buffer_free_indices.top();
        buffer_free_indices.pop();
        buffer_add_cache.emplace_back(name, index);
        buffers[index] = buffer;
        return index;
    }
    buffer_names.push_back(name);
    buffer_add_cache.emplace_back(name, (uint32_t)buffers.size());
    buffers.push_back(buffer);
    return buffers.size() - 1;
}
EPIX_API uint32_t
VulkanResources::add_image(const std::string& name, Image image) {
    if (image_map.contains(name)) {
        return image_map[name];
    }
    if (!image_free_indices.empty()) {
        auto index = image_free_indices.top();
        image_free_indices.pop();
        image_add_cache.emplace_back(name, index);
        images[index] = image;
        return index;
    }
    image_names.push_back(name);
    image_add_cache.emplace_back(name, (uint32_t)images.size());
    images.push_back(image);
    return images.size() - 1;
}
EPIX_API uint32_t
VulkanResources::add_image_view(const std::string& name, ImageView image_view) {
    if (image_view_map.contains(name)) {
        return image_view_map[name];
    }
    if (!view_free_indices.empty()) {
        auto index = view_free_indices.top();
        view_free_indices.pop();
        view_add_cache.emplace_back(name, index);
        image_views[index] = image_view;
        return index;
    }
    image_view_names.push_back(name);
    view_add_cache.emplace_back(name, (uint32_t)image_views.size());
    image_views.push_back(image_view);
    view_cache.emplace_back((uint32_t)image_views.size() - 1, image_view);
    return image_views.size() - 1;
}
EPIX_API uint32_t
VulkanResources::add_sampler(const std::string& name, Sampler sampler) {
    if (sampler_map.contains(name)) {
        return sampler_map[name];
    }
    if (!sampler_free_indices.empty()) {
        auto index = sampler_free_indices.top();
        sampler_free_indices.pop();
        sampler_add_cache.emplace_back(name, index);
        samplers[index] = sampler;
        return index;
    }
    sampler_names.push_back(name);
    sampler_add_cache.emplace_back(name, (uint32_t)samplers.size());
    samplers.push_back(sampler);
    sampler_cache.emplace_back((uint32_t)samplers.size() - 1, sampler);
    return samplers.size() - 1;
}
EPIX_API void VulkanResources::apply_cache() {
    for (auto&& [name, index] : buffer_add_cache) {
        buffer_map.emplace(std::move(name), index);
    }
    for (auto&& [name, index] : image_add_cache) {
        image_map.emplace(std::move(name), index);
    }
    for (auto&& [name, index] : view_add_cache) {
        image_view_map.emplace(std::move(name), index);
    }
    for (auto&& [name, index] : sampler_add_cache) {
        sampler_map.emplace(std::move(name), index);
    }
    buffer_add_cache.clear();
    image_add_cache.clear();
    view_add_cache.clear();
    sampler_add_cache.clear();
    for (auto index : buffer_cache_remove) {
        m_device.destroyBuffer(buffers[index]);
        buffer_free_indices.push(index);
    }
    for (auto index : image_cache_remove) {
        m_device.destroyImage(images[index]);
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
    m_device.updateDescriptorSets(descriptor_writes, {});
    image_infos.clear();
    descriptor_writes.clear();
    for (auto index : view_cache_remove) {
        m_device.destroyImageView(image_views[index]);
        view_free_indices.push(index);
    }
    for (auto index : sampler_cache_remove) {
        m_device.destroySampler(samplers[index]);
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
    auto index = buffer_index(name);
    if (index == -1) {
        return Buffer();
    }
    return buffers[index];
}
EPIX_API Image VulkanResources::get_image(const std::string& name) const {
    auto index = image_index(name);
    if (index == -1) {
        return Image();
    }
    return images[index];
}
EPIX_API ImageView VulkanResources::get_image_view(const std::string& name
) const {
    auto index = image_view_index(name);
    if (index == -1) {
        return ImageView();
    }
    return image_views[index];
}
EPIX_API Sampler VulkanResources::get_sampler(const std::string& name) const {
    auto index = sampler_index(name);
    if (index == -1) {
        return Sampler();
    }
    return samplers[index];
}
EPIX_API Buffer VulkanResources::get_buffer(uint32_t index) const {
    return buffers[index];
}
EPIX_API Image VulkanResources::get_image(uint32_t index) const {
    return images[index];
}
EPIX_API ImageView VulkanResources::get_image_view(uint32_t index) const {
    return image_views[index];
}
EPIX_API Sampler VulkanResources::get_sampler(uint32_t index) const {
    return samplers[index];
}
EPIX_API void VulkanResources::remove_buffer(const std::string& name) {
    if (!buffer_map.contains(name)) {
        return;
    }
    auto index = buffer_map[name];
    buffer_cache_remove.push_back(index);
}
EPIX_API void VulkanResources::remove_image(const std::string& name) {
    if (!image_map.contains(name)) {
        return;
    }
    auto index = image_map[name];
    image_cache_remove.push_back(index);
}
EPIX_API void VulkanResources::remove_image_view(const std::string& name) {
    if (!image_view_map.contains(name)) {
        return;
    }
    auto index = image_view_map[name];
    view_cache_remove.push_back(index);
}
EPIX_API void VulkanResources::remove_sampler(const std::string& name) {
    if (!sampler_map.contains(name)) {
        return;
    }
    auto index = sampler_map[name];
    sampler_cache_remove.push_back(index);
}
EPIX_API void VulkanResources::remove_buffer(uint32_t index) {
    buffer_cache_remove.push_back(index);
}
EPIX_API void VulkanResources::remove_image(uint32_t index) {
    image_cache_remove.push_back(index);
}
EPIX_API void VulkanResources::remove_image_view(uint32_t index) {
    view_cache_remove.push_back(index);
}
EPIX_API void VulkanResources::remove_sampler(uint32_t index) {
    sampler_cache_remove.push_back(index);
}
EPIX_API uint32_t VulkanResources::buffer_index(const std::string& name) const {
    if (!buffer_map.contains(name)) {
        return -1;
    }
    return buffer_map.at(name);
}
EPIX_API uint32_t VulkanResources::image_index(const std::string& name) const {
    if (!image_map.contains(name)) {
        return -1;
    }
    return image_map.at(name);
}
EPIX_API uint32_t VulkanResources::image_view_index(const std::string& name
) const {
    if (!image_view_map.contains(name)) {
        return -1;
    }
    return image_view_map.at(name);
}
EPIX_API uint32_t VulkanResources::sampler_index(const std::string& name
) const {
    if (!sampler_map.contains(name)) {
        return -1;
    }
    return sampler_map.at(name);
}
EPIX_API vk::DescriptorSet VulkanResources::get_descriptor_set() const {
    return descriptor_set;
}
EPIX_API vk::DescriptorSetLayout VulkanResources::get_descriptor_set_layout(
) const {
    return descriptor_set_layout;
}
}  // namespace epix::render::vulkan2