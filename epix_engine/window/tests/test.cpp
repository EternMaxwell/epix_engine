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
        .mark_frame        = false,
        .enable_tracy      = false,
    });

    struct FrameCounter {
        int count = 0;
    };

    // app.world(epix::app::MainWorld).spawn(window_desc2);
    app.add_plugins(epix::window::WindowPlugin{});
    app.get_plugin<epix::window::WindowPlugin>()->primary_window = window_desc;
    app.add_plugins(epix::glfw::GLFWPlugin{});
    app.add_systems(
        epix::Update, epix::into([](epix::Local<FrameCounter> count) {
            std::cout << "Frame: " << count->count++ << std::endl;
        })
    );
    app.run();
}