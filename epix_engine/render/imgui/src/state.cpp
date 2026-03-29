module;

#include <imgui.h>

module epix.render.imgui;

namespace epix::imgui {

void ImGuiState::activate() const {
    if (ctx) {
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx));
    }
}

}  // namespace epix::imgui
