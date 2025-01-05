#include <epix/app.h>
#include <epix/window.h>

int main() {
    using namespace epix::app;
    using namespace epix::window;

    App app2 = App::create2();
    app2.enable_loop();
    app2.add_plugin(WindowPlugin{});
    app2.run();
}