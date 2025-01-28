#pragma once

#include <epix/common.h>
// #define IMGUI_API EPIX_API
#include <epix/rdvk.h>
#include <epix/window.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

namespace epix::imgui {
using namespace epix::render::vulkan2;
using namespace epix::render::vulkan2::backend;
using namespace epix::window::components;
struct ImGuiContext {
    DescriptorPool descriptor_pool;
    RenderPass render_pass;
    CommandBuffer command_buffer;
    Fence fence;
    Framebuffer framebuffer;
    ::ImGuiContext* context;

    EPIX_API ::ImGuiContext* current_context();

    void sync_context() { ImGui::SetCurrentContext(current_context()); }
};
}  // namespace epix::imgui