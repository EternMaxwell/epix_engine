#pragma once

#include <epix/app.h>
#include <epix/common.h>
#include <epix/window.h>

#include "epix/rdvk2/rdvk_basic.h"
#include "epix/rdvk2/rdvk_utils.h"

namespace epix::render::vulkan2 {
struct RenderContext_T {
    backend::Instance instance;
    mutable backend::PhysicalDevice physical_device;
    mutable backend::Device device;
    backend::Queue queue;
    mutable backend::CommandPool command_pool;
    mutable backend::Surface primary_surface;
    mutable backend::Swapchain primary_swapchain;
};
struct RenderContext;
struct VulkanPlugin;
struct ContextCmd_T {
    backend::CommandBuffer cmd_buffer;
    backend::Fence fence;
};
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
   private:
    RenderContext_T* context;

   public:
    RenderContext() : context(nullptr) {}
    backend::Instance& instance() { return context->instance; }
    backend::PhysicalDevice& physical_device() const {
        return context->physical_device;
    }
    backend::Device& device() const { return context->device; }
    backend::Queue& queue() { return context->queue; }
    backend::CommandPool& command_pool() const { return context->command_pool; }
    backend::Surface& primary_surface() const {
        return context->primary_surface;
    }
    backend::Swapchain& primary_swapchain() const {
        return context->primary_swapchain;
    }
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
   private:
    ContextCmd_T* cmd;

   public:
    CtxCmdBuffer() : cmd(nullptr) {}
    backend::CommandBuffer& cmd_buffer() { return cmd->cmd_buffer; }
    backend::Fence& fence() { return cmd->fence; }
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
