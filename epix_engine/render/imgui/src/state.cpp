module;

#include <imgui.h>

module epix.render.imgui;

void imgui::ImGuiState::activate() const {
    if (ctx) {
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx));
    }
}
