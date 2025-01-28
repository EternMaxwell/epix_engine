#include <epix/imgui.h>
#include <epix/input.h>
#include <epix/rdvk.h>
#include <epix/window.h>

using namespace epix;

void show_test_window(Res<imgui::ImGuiContext> ctx) { ImGui::ShowDemoWindow(); }

int main() {
    App app = App::create2();
    app.add_plugin(window::WindowPlugin{});
    app.get_plugin<window::WindowPlugin>()->primary_desc().set_vsync(false);
    app.add_plugin(render::vulkan2::VulkanPlugin{}.set_debug_callback(true));
    app.add_plugin(imgui::ImGuiPluginVK{});
    app.add_plugin(input::InputPlugin{});

    app.add_system(Render, show_test_window);

    app.run();
}