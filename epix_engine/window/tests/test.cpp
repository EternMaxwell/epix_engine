#include "epix/window.h"

int main() {
    if (glfwInit() == GLFW_FALSE) {
        return -1;
    }
    glfwDefaultWindowHints();

    using namespace epix::window;
    using namespace epix::glfw;
    using namespace epix::window::window;

    Window window_desc;
    Window cached_desc = window_desc;
    window_desc.title  = "Test Window";

    window_desc.set_size(800, 600);
    window_desc.opacity = 0.5f;

    auto window = create_window(epix::Entity{}, window_desc);
    UserData* user_data =
        static_cast<UserData*>(glfwGetWindowUserPointer(window));
    if (!user_data) {
        throw std::runtime_error("Failed to get user data from window");
    }

    while (!glfwWindowShouldClose(window)) {
        sync_window_to_glfw(epix::Entity{}, window_desc, cached_desc, window);
        glfwWaitEvents();
        sync_glfw_to_window(epix::Entity{}, window_desc, cached_desc, window);
        // check for events
        while (auto key_input = user_data->key_input.try_pop()) {
            auto [key, scancode, action, mods] = *key_input;
            auto key_name                      = glfwGetKeyName(key, scancode);
            auto mods_name                     = glfwGetKeyName(mods, 0);
            auto action_name =
                action == GLFW_PRESS
                    ? "Pressed"
                    : (action == GLFW_RELEASE ? "Released" : "Unknown");
            if (key_name == nullptr) {
                key_name = "Unknown";
            }
            if (mods_name == nullptr) {
                mods_name = "Unknown";
            }
            std::cout << "Key: " << key_name << ", Action: " << action_name
                      << ", Mods: " << mods_name << std::endl;
            if (key == GLFW_KEY_F11 && action == GLFW_PRESS) {
                window_desc.mode = window_desc.mode == WindowMode::Windowed
                                       ? WindowMode::BorderlessFullscreen
                                       : WindowMode::Windowed;
            }
            if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        }
        // std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // window_desc.position->x += 1;
        // window_desc.position->y += 1;
        // while (auto cursor_pos = user_data->cursor_pos.try_pop()) {
        //     auto [x, y] = *cursor_pos;
        //     std::cout << "Cursor Position: (" << x << ", " << y << ")"
        //               << std::endl;
        // }
        {
            auto [in, x, y] = window_desc.cursor_position();
            std::cout << "Cursor Position: (" << x << ", " << y << ")"
                      << ", In Window: " << (in ? "Yes" : "No") << std::endl;
        }
        {
            // window pos and window size
            auto [x, y]          = window_desc.get_position();
            auto [width, height] = window_desc.size();
            std::cout << "Window Position: (" << x << ", " << y << "), Size: ("
                      << width << ", " << height << ")" << std::endl;
        }
        while (auto cursor_enter = user_data->cursor_enter.try_pop()) {
            auto entered = *cursor_enter;
            std::cout << "Cursor Entered: " << (entered.entered ? "Yes" : "No")
                      << std::endl;
        }
        while (auto mouse_button = user_data->mouse_button.try_pop()) {
            auto [button, action, mods] = *mouse_button;
            auto action_name =
                action == GLFW_PRESS
                    ? "Pressed"
                    : (action == GLFW_RELEASE ? "Released" : "Unknown");
            std::cout << "Mouse Button: " << button
                      << ", Action: " << action_name << std::endl;
        }
        while (auto scroll = user_data->scroll.try_pop()) {
            auto [xoffset, yoffset] = *scroll;
            std::cout << "Scroll: (" << xoffset << ", " << yoffset << ")"
                      << std::endl;
        }
        while (auto file_drop = user_data->drops.try_pop()) {
            auto paths = *file_drop;
            std::cout << "File Drop: ";
            for (const auto& path : paths.paths) {
                std::cout << path << " ";
            }
            std::cout << std::endl;
        }
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}