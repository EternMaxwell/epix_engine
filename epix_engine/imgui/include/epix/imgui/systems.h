#pragma once

#include <epix/common.h>
// #define IMGUI_API EPIX_API
#include <epix/rdvk.h>
#include <epix/window.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include "resources.h"

namespace epix::imgui::systems {
using namespace epix::prelude;
using namespace epix::render::vulkan2;
using namespace epix::window::components;
using namespace epix::imgui;
EPIX_SYSTEMT(EPIX_API void, insert_imgui_ctx, (Command cmd))
EPIX_SYSTEMT(
    EPIX_API void,
    init_imgui,
    (ResMut<render::vulkan2::RenderContext> context,
     Query<Get<Window>, With<PrimaryWindow>> window_query,
     ResMut<ImGuiContext> imgui_context)
)
EPIX_SYSTEMT(
    EPIX_API void,
    deinit_imgui,
    (ResMut<render::vulkan2::RenderContext> context,
     ResMut<ImGuiContext> imgui_context)
)
EPIX_SYSTEMT(
    EPIX_API void,
    extract_imgui_ctx,
    (Command cmd, Extract<ResMut<ImGuiContext>> imgui_context)
)
EPIX_SYSTEMT(
    EPIX_API void,
    begin_imgui,
    (ResMut<ImGuiContext> ctx, Res<render::vulkan2::RenderContext> context)
)
EPIX_SYSTEMT(
    EPIX_API void,
    end_imgui,
    (ResMut<ImGuiContext> ctx, Res<render::vulkan2::RenderContext> context)
)
}  // namespace epix::imgui::systems