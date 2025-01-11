#pragma once

#include <epix/app.h>
#include <epix/common.h>
#include <epix/window.h>

#include "epix/rdvk2/rdvk_basic.h"
#include "epix/rdvk2/rdvk_utils.h"

namespace epix::render::vulkan2 {
struct RenderContext {};
struct RenderVKPlugin;
struct ContextCommandBuffer {};
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
    Res<RenderVKPlugin> plugin
);
EPIX_API void destroy_context(
    Command cmd,
    Query<
        Get<Instance, Device, Surface, Swapchain, CommandPool>,
        With<RenderContext>> query,
    Query<Get<CommandBuffer, Fence>, With<ContextCommandBuffer>> cmd_query
);
EPIX_API void extract_context(
    Extract<
        Get<Instance, Device, Surface, Swapchain, CommandPool, Queue>,
        With<RenderContext>> query,
    Extract<Get<CommandBuffer, Fence>, With<ContextCommandBuffer>> cmd_query,
    Command cmd
);
EPIX_API void clear_extracted_context(
    Query<Get<Entity>, With<RenderContext>> query,
    Query<Get<Entity>, With<ContextCommandBuffer>> cmd_query,
    Command cmd
);
EPIX_API void recreate_swap_chain(
    Query<Get<Swapchain>, With<RenderContext>> query
);
EPIX_API void get_next_image(
    Query<Get<Device, Swapchain, CommandPool, Queue>, With<RenderContext>>
        query,
    Query<Get<CommandBuffer, Fence>, With<ContextCommandBuffer>> cmd_query
);
EPIX_API void present_frame(
    Query<Get<Swapchain, Queue, Device, CommandPool>, With<RenderContext>>
        query,
    Query<Get<CommandBuffer, Fence>, With<ContextCommandBuffer>> cmd_query
);
}  // namespace systems
struct RenderVKPlugin : public epix::Plugin {
    bool debug_callback = false;
    EPIX_API RenderVKPlugin& set_debug_callback(bool debug);
    EPIX_API void build(epix::App& app) override;
};
}  // namespace epix::render::vulkan2
