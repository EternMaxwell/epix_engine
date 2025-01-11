#pragma once

#include "rdvk.h"

namespace epix::render::vulkan2 {
struct ResourceManager {
    using Device               = backend::Device;
    using Buffer               = backend::Buffer;
    using Image                = backend::Image;
    using ImageView            = backend::ImageView;
    using Sampler              = backend::Sampler;
    using AllocationCreateInfo = backend::AllocationCreateInfo;

    Device device;

    std::vector<Buffer> buffers;
    std::vector<std::string> buffer_names;
    entt::dense_map<std::string, uint32_t> buffer_map;

    std::vector<Image> images;
    std::vector<std::string> image_names;
    entt::dense_map<std::string, uint32_t> image_map;

    std::vector<ImageView> image_views;
    std::vector<std::string> image_view_names;
    entt::dense_map<std::string, uint32_t> image_view_map;

    std::vector<Sampler> samplers;
    std::vector<std::string> sampler_names;
    entt::dense_map<std::string, uint32_t> sampler_map;

    EPIX_API void destroy();
    EPIX_API uint32_t create_buffer(
        const std::string& name,
        vk::BufferCreateInfo& create_info,
        AllocationCreateInfo& alloc_info
    );
    EPIX_API uint32_t add_buffer(const std::string& name, Buffer buffer);
    EPIX_API uint32_t create_image(
        const std::string& name,
        vk::ImageCreateInfo& create_info,
        AllocationCreateInfo& alloc_info
    );
    EPIX_API uint32_t add_image(const std::string& name, Image image);
    EPIX_API uint32_t create_image_view(
        const std::string& name, vk::ImageViewCreateInfo& create_info
    );
    EPIX_API uint32_t
    add_image_view(const std::string& name, ImageView image_view);
    EPIX_API uint32_t
    create_sampler(const std::string& name, vk::SamplerCreateInfo& create_info);
    EPIX_API uint32_t add_sampler(const std::string& name, Sampler sampler);

    EPIX_API Buffer get_buffer(const std::string& name) const;
    EPIX_API Image get_image(const std::string& name) const;
    EPIX_API ImageView get_image_view(const std::string& name) const;
    EPIX_API Sampler get_sampler(const std::string& name) const;
    EPIX_API Buffer get_buffer(uint32_t index) const;
    EPIX_API Image get_image(uint32_t index) const;
    EPIX_API ImageView get_image_view(uint32_t index) const;
    EPIX_API Sampler get_sampler(uint32_t index) const;

    EPIX_API void remove_buffer(const std::string& name);
    EPIX_API void remove_image(const std::string& name);
    EPIX_API void remove_image_view(const std::string& name);
    EPIX_API void remove_sampler(const std::string& name);
    EPIX_API void remove_buffer(uint32_t index);
    EPIX_API void remove_image(uint32_t index);
    EPIX_API void remove_image_view(uint32_t index);
    EPIX_API void remove_sampler(uint32_t index);

    EPIX_API uint32_t buffer_index(const std::string& name) const;
    EPIX_API uint32_t image_index(const std::string& name) const;
    EPIX_API uint32_t image_view_index(const std::string& name) const;
    EPIX_API uint32_t sampler_index(const std::string& name) const;
};
struct RenderContextResManager {};
namespace systems {
using namespace epix::render::vulkan2::backend;
using epix::Command;
using epix::Extract;
using epix::Get;
using epix::Query;
using epix::Res;
using epix::ResMut;
using epix::With;
using epix::Without;

EPIX_API void create_res_manager(
    Command cmd, Query<Get<Device>, With<RenderContext>> query
);
EPIX_API void destroy_res_manager(
    Command cmd,
    Query<Get<vulkan2::ResourceManager>, With<RenderContextResManager>> query
);
EPIX_API void extract_res_manager(
    Extract<
        Get<Entity, vulkan2::ResourceManager>,
        With<RenderContextResManager>> query,
    Query<
        Get<Wrapper<const vulkan2::ResourceManager>>,
        With<RenderContextResManager>> render_query,
    Command cmd
);
}  // namespace systems
struct VulkanResManagerPlugin : epix::Plugin {
    EPIX_API void build(epix::App& app) override;
};
}  // namespace epix::render::vulkan2