#include <epix/app.h>
#include <epix/input.h>
#include <epix/rdvk.h>
#include <epix/window.h>

int main() {
    using namespace epix::app;
    using namespace epix::window;

    App app2 = App::create2();
    app2.add_plugin(WindowPlugin{});
    app2.add_plugin(epix::render::vulkan2::RenderVKPlugin{}.set_vsync(true));
    app2.add_plugin(epix::input::InputPlugin{}.enable_output());
    app2.run();
}