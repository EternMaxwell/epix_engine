#include "epix/rdvk_res.h"

namespace epix::render::vulkan2 {
using Device               = backend::Device;
using Buffer               = backend::Buffer;
using Image                = backend::Image;
using ImageView            = backend::ImageView;
using Sampler              = backend::Sampler;
using AllocationCreateInfo = backend::AllocationCreateInfo;
EPIX_API ResourceManager::ResourceManager(Device device) : device(device) {
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
EPIX_API void ResourceManager::destroy() {
    device.destroyDescriptorSetLayout(descriptor_set_layout);
    device.destroyDescriptorPool(descriptor_pool);
    for (auto& buffer : buffers) {
        buffer.destroy();
    }
    for (auto& image : images) {
        image.destroy();
    }
    for (auto& image_view : image_views) {
        device.destroyImageView(image_view);
    }
    for (auto& sampler : samplers) {
        device.destroySampler(sampler);
    }
}
EPIX_API uint32_t
ResourceManager::add_buffer(const std::string& name, Buffer buffer) {
    if (buffer_map.contains(name)) {
        return buffer_map[name];
    }
    buffer_names.push_back(name);
    buffer_map[name] = buffers.size();
    buffers.push_back(buffer);
    return buffers.size() - 1;
}
EPIX_API uint32_t
ResourceManager::add_image(const std::string& name, Image image) {
    if (image_map.contains(name)) {
        return image_map[name];
    }
    image_names.push_back(name);
    image_map[name] = images.size();
    images.push_back(image);
    return images.size() - 1;
}
EPIX_API uint32_t
ResourceManager::add_image_view(const std::string& name, ImageView image_view) {
    if (image_view_map.contains(name)) {
        return image_view_map[name];
    }
    image_view_names.push_back(name);
    image_view_map[name] = image_views.size();
    image_views.push_back(image_view);
    view_cache.emplace_back(image_views.size() - 1, image_view);
    return image_views.size() - 1;
}
EPIX_API uint32_t
ResourceManager::add_sampler(const std::string& name, Sampler sampler) {
    if (sampler_map.contains(name)) {
        return sampler_map[name];
    }
    sampler_names.push_back(name);
    sampler_map[name] = samplers.size();
    samplers.push_back(sampler);
    sampler_cache.emplace_back(samplers.size() - 1, sampler);
    return samplers.size() - 1;
}
EPIX_API void ResourceManager::apply_cache() {
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
    view_cache.clear();
    sampler_cache.clear();
    image_infos.clear();
    descriptor_writes.clear();
    image_infos.reserve(view_cache_remove.size() + sampler_cache_remove.size());
    descriptor_writes.reserve(
        view_cache_remove.size() + sampler_cache_remove.size()
    );
    for (auto index : view_cache_remove) {
        image_infos.push_back(
            vk::DescriptorImageInfo()
                .setImageView(image_views[index])
                .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
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
}
EPIX_API Buffer ResourceManager::get_buffer(const std::string& name) const {
    auto index = buffer_index(name);
    if (index == -1) {
        return Buffer();
    }
    return buffers[index];
}
EPIX_API Image ResourceManager::get_image(const std::string& name) const {
    auto index = image_index(name);
    if (index == -1) {
        return Image();
    }
    return images[index];
}
EPIX_API ImageView ResourceManager::get_image_view(const std::string& name
) const {
    auto index = image_view_index(name);
    if (index == -1) {
        return ImageView();
    }
    return image_views[index];
}
EPIX_API Sampler ResourceManager::get_sampler(const std::string& name) const {
    auto index = sampler_index(name);
    if (index == -1) {
        return Sampler();
    }
    return samplers[index];
}
EPIX_API Buffer ResourceManager::get_buffer(uint32_t index) const {
    return buffers[index];
}
EPIX_API Image ResourceManager::get_image(uint32_t index) const {
    return images[index];
}
EPIX_API ImageView ResourceManager::get_image_view(uint32_t index) const {
    return image_views[index];
}
EPIX_API Sampler ResourceManager::get_sampler(uint32_t index) const {
    return samplers[index];
}
EPIX_API void ResourceManager::remove_buffer(const std::string& name) {
    if (!buffer_map.contains(name)) {
        return;
    }
    auto index = buffer_map[name];
    remove_buffer(index);
}
EPIX_API void ResourceManager::remove_image(const std::string& name) {
    if (!image_map.contains(name)) {
        return;
    }
    auto index = image_map[name];
    remove_image(index);
}
EPIX_API void ResourceManager::remove_image_view(const std::string& name) {
    if (!image_view_map.contains(name)) {
        return;
    }
    auto index = image_view_map[name];
    remove_image_view(index);
}
EPIX_API void ResourceManager::remove_sampler(const std::string& name) {
    if (!sampler_map.contains(name)) {
        return;
    }
    auto index = sampler_map[name];
    remove_sampler(index);
}
EPIX_API void ResourceManager::remove_buffer(uint32_t index) {
    device.destroy_buffer(buffers[index]);
    std::swap(buffers[index], buffers.back());
    std::swap(buffer_names[index], buffer_names.back());
    buffer_map[buffer_names[index]] = index;
    buffer_map.erase(buffer_names.back());
    buffers.pop_back();
    buffer_names.pop_back();
}
EPIX_API void ResourceManager::remove_image(uint32_t index) {
    device.destroy_image(images[index]);
    std::swap(images[index], images.back());
    std::swap(image_names[index], image_names.back());
    image_map[image_names[index]] = index;
    image_map.erase(image_names.back());
    images.pop_back();
    image_names.pop_back();
}
EPIX_API void ResourceManager::remove_image_view(uint32_t index) {
    device.destroyImageView(image_views[index]);
    std::swap(image_views[index], image_views.back());
    std::swap(image_view_names[index], image_view_names.back());
    image_view_map[image_view_names[index]] = index;
    image_view_map.erase(image_view_names.back());
    image_views.pop_back();
    image_view_names.pop_back();
    view_cache_remove.push_back(index);
}
EPIX_API void ResourceManager::remove_sampler(uint32_t index) {
    device.destroySampler(samplers[index]);
    std::swap(samplers[index], samplers.back());
    std::swap(sampler_names[index], sampler_names.back());
    sampler_map[sampler_names[index]] = index;
    sampler_map.erase(sampler_names.back());
    samplers.pop_back();
    sampler_names.pop_back();
    sampler_cache_remove.push_back(index);
}
EPIX_API uint32_t ResourceManager::buffer_index(const std::string& name) const {
    if (!buffer_map.contains(name)) {
        return -1;
    }
    return buffer_map.at(name);
}
EPIX_API uint32_t ResourceManager::image_index(const std::string& name) const {
    if (!image_map.contains(name)) {
        return -1;
    }
    return image_map.at(name);
}
EPIX_API uint32_t ResourceManager::image_view_index(const std::string& name
) const {
    if (!image_view_map.contains(name)) {
        return -1;
    }
    return image_view_map.at(name);
}
EPIX_API uint32_t ResourceManager::sampler_index(const std::string& name
) const {
    if (!sampler_map.contains(name)) {
        return -1;
    }
    return sampler_map.at(name);
}
EPIX_API vk::DescriptorSet ResourceManager::get_descriptor_set() const {
    return descriptor_set;
}
EPIX_API vk::DescriptorSetLayout ResourceManager::get_descriptor_set_layout(
) const {
    return descriptor_set_layout;
}
EPIX_API void VulkanResManagerPlugin::build(epix::App& app) {
    app.add_system(epix::PreStartup, systems::create_res_manager)
        .after(vulkan2::systems::create_context);
    app.add_system(epix::Extraction, systems::extract_res_manager);
    app.add_system(epix::Exit, systems::destroy_res_manager);
}
}  // namespace epix::render::vulkan2