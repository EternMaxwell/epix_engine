
#include <imgui.h>
#include <spdlog/spdlog.h>
#include <epix/render/imgui.hpp>

namespace epix::imgui {

void ImGuiState::activate() const {
    if (ctx) {
        spdlog::trace("[imgui] Activating ImGui context.");
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx));
    }
}

}  // namespace epix::imgui
