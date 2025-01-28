#include "epix/imgui.h"

using namespace epix::prelude;
using namespace epix;

EPIX_API ImGuiContext* imgui::ImGuiContext::current_context() {
    return ImGui::GetCurrentContext();
}