#include "epix/input.h"
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
    Window window_desc2;

    window_desc.title = "Test Window";

    window_desc.set_size(800, 200);
    window_desc.opacity = 0.5f;

    window_desc2.title = "Test Window 2";

    window_desc2.set_size(800, 400);
    window_desc2.opacity = 0.7f;

    epix::App app = epix::App::create(epix::AppConfig{
        .mark_frame   = false,
        .enable_tracy = false,
    });

    struct FrameCounter {
        int count = 0;
    };

    app.world(epix::app::MainWorld).spawn(window_desc2);
    app.add_plugins(epix::window::WindowPlugin{});
    app.get_plugin<epix::window::WindowPlugin>()->primary_window = window_desc;
    app.get_plugin<epix::window::WindowPlugin>()->exit_condition =
        epix::window::ExitCondition::OnAllClosed;
    app.add_plugins(epix::glfw::GLFWPlugin{});
    app.add_plugins(epix::input::InputPlugin{});
    app.add_systems(
        epix::Update,
        epix::into(epix::input::print_inputs, epix::window::print_events)
            .set_name("print inputs")
    );
    app.add_systems(
        epix::Update,
        epix::into([](epix::EventReader<epix::input::KeyInput> key_reader,
                      epix::Query<epix::Get<epix::window::Window>> windows) {
            for (auto&& [key, scancode, pressed, repeat, window] :
                 key_reader.read()) {
                if (pressed) {
                    if (key == epix::input::KeyCode::KeyF11) {
                        if (auto window_opt = windows.get(window)) {
                            auto&& [window_desc] = *window_opt;
                            if (window_desc.mode ==
                                epix::window::window::WindowMode::Windowed) {
                                window_desc.mode = epix::window::window::
                                    WindowMode::Fullscreen;
                            } else {
                                window_desc.mode =
                                    epix::window::window::WindowMode::Windowed;
                            }
                        }
                    }
                }
            }
        })
    );
    // app.add_systems(
    //     epix::Update, epix::into([](epix::Local<FrameCounter> count) {
    //         std::cout << "Frame: " << count->count++ << std::endl;
    //     })
    // );
    app.run();
}