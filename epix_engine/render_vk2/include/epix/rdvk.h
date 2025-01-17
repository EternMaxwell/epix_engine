#pragma once

#include <epix/app.h>
#include <epix/common.h>
#include <epix/window.h>

#include <stack>

#include "epix/rdvk2/rdvk_basic.h"
#include "epix/rdvk2/rdvk_utils.h"

namespace epix::render::vulkan2 {
struct RenderContext;
struct VulkanPlugin;
struct CtxCmdBuffer;
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
using window::components::PrimaryWindow;
using window::components::Window;
EPIX_API void create_context(
    Command cmd,
    Query<Get<Window>, With<PrimaryWindow>> query,
    Res<VulkanPlugin> plugin
);
EPIX_API void destroy_context(
    Command cmd, ResMut<RenderContext> context, ResMut<CtxCmdBuffer> ctx_cmd
);
}  // namespace systems
struct RenderContext {
   public:
    backend::Instance instance;
    mutable backend::PhysicalDevice physical_device;
    mutable backend::Device device;
    backend::Queue queue;
    mutable backend::CommandPool command_pool;
    mutable backend::Surface primary_surface;
    mutable backend::Swapchain primary_swapchain;

    friend EPIX_API void systems::create_context(
        Command cmd,
        Query<
            Get<window::components::Window>,
            With<window::components::PrimaryWindow>> query,
        Res<VulkanPlugin> plugin
    );
    friend EPIX_API void systems::destroy_context(
        Command cmd, ResMut<RenderContext> context, ResMut<CtxCmdBuffer> ctx_cmd
    );
};
struct CtxCmdBuffer {
   public:
    backend::CommandBuffer cmd_buffer;
    backend::Fence fence;

    friend EPIX_API void systems::create_context(
        Command cmd,
        Query<
            Get<window::components::Window>,
            With<window::components::PrimaryWindow>> query,
        Res<VulkanPlugin> plugin
    );
    friend EPIX_API void systems::destroy_context(
        Command cmd, ResMut<RenderContext> context, ResMut<CtxCmdBuffer> ctx_cmd
    );
};
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
using window::components::PrimaryWindow;
using window::components::Window;
EPIX_API void extract_context(
    ResMut<RenderContext> context, ResMut<CtxCmdBuffer> ctx_cmd, Command cmd
);
EPIX_API void clear_extracted_context(
    ResMut<RenderContext> context, ResMut<CtxCmdBuffer> ctx_cmd, Command cmd
);
EPIX_API void recreate_swap_chain(
    ResMut<RenderContext> context, ResMut<CtxCmdBuffer> ctx_cmd
);
EPIX_API void get_next_image(
    ResMut<RenderContext> context, ResMut<CtxCmdBuffer> ctx_cmd
);
EPIX_API void present_frame(
    ResMut<RenderContext> context, ResMut<CtxCmdBuffer> ctx_cmd
);
}  // namespace systems
struct VulkanPlugin : public epix::Plugin {
    bool debug_callback = false;
    EPIX_API VulkanPlugin& set_debug_callback(bool debug);
    EPIX_API void build(epix::App& app) override;
};
}  // namespace epix::render::vulkan2

namespace epix::render::vulkan2 {
struct VulkanResources;
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

EPIX_API void create_res_manager(Command cmd, Res<RenderContext> context);
EPIX_API void destroy_res_manager(
    Command cmd, ResMut<VulkanResources> res_manager
);
EPIX_API void extract_res_manager(
    ResMut<VulkanResources> res_manager, Command cmd
);
EPIX_API void clear_extracted(ResMut<VulkanResources> res_manager, Command cmd);
}  // namespace systems
struct VulkanResources {
    using Device    = backend::Device;
    using Buffer    = backend::Buffer;
    using Image     = backend::Image;
    using ImageView = backend::ImageView;
    using Sampler   = backend::Sampler;

   private:
    mutable Device m_device;

    std::vector<Buffer> buffers;
    std::vector<std::string> buffer_names;
    entt::dense_map<std::string, uint32_t> buffer_map;
    entt::dense_set<uint32_t> buffer_cache_remove;
    std::vector<std::pair<std::string, uint32_t>> buffer_add_cache;
    std::stack<uint32_t> buffer_free_indices;

    std::vector<Image> images;
    std::vector<std::string> image_names;
    entt::dense_map<std::string, uint32_t> image_map;
    entt::dense_set<uint32_t> image_cache_remove;
    std::vector<std::pair<std::string, uint32_t>> image_add_cache;
    std::stack<uint32_t> image_free_indices;

    std::vector<ImageView> image_views;
    std::vector<std::string> image_view_names;
    entt::dense_map<std::string, uint32_t> image_view_map;
    std::vector<std::pair<uint32_t, ImageView>> view_cache;
    entt::dense_set<uint32_t> view_cache_remove;
    std::vector<std::pair<std::string, uint32_t>> view_add_cache;
    std::stack<uint32_t> view_free_indices;

    std::vector<Sampler> samplers;
    std::vector<std::string> sampler_names;
    entt::dense_map<std::string, uint32_t> sampler_map;
    std::vector<std::pair<uint32_t, Sampler>> sampler_cache;
    entt::dense_set<uint32_t> sampler_cache_remove;
    std::vector<std::pair<std::string, uint32_t>> sampler_add_cache;
    std::stack<uint32_t> sampler_free_indices;

    vk::DescriptorPool descriptor_pool;
    vk::DescriptorSetLayout descriptor_set_layout;
    vk::DescriptorSet descriptor_set;

    EPIX_API VulkanResources(Device device);
    EPIX_API void apply_cache();

   public:
    EPIX_API Device& device() const;
    EPIX_API void destroy();
    EPIX_API uint32_t add_buffer(const std::string& name, Buffer buffer);
    EPIX_API uint32_t add_image(const std::string& name, Image image);
    EPIX_API uint32_t
    add_image_view(const std::string& name, ImageView image_view);
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

    EPIX_API vk::DescriptorSet get_descriptor_set() const;
    EPIX_API vk::DescriptorSetLayout get_descriptor_set_layout() const;

    friend EPIX_API void systems::create_res_manager(
        Command cmd, Res<RenderContext> context
    );
    friend EPIX_API void systems::destroy_res_manager(
        Command cmd, ResMut<VulkanResources> res_manager
    );
    friend EPIX_API void systems::extract_res_manager(
        ResMut<VulkanResources> res_manager, Command cmd
    );
};
}  // namespace epix::render::vulkan2