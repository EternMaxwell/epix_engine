#include <epix/app.h>
#include <epix/input.h>
#include <epix/wgpu.h>
#include <epix/window.h>

using namespace epix;

struct Context {
    WGPUInstance instance;
};

template <typename T>
std::remove_reference_t<T>* addressof(T&& arg) {
    return reinterpret_cast<std::remove_reference_t<T>*>(
        &const_cast<char&>(reinterpret_cast<const volatile char&>(arg))
    );
}

void insert_context(epix::Command cmd) { cmd.insert_resource(Context{}); }

void setup_context(epix::ResMut<Context> ctx) {
    ctx->instance = wgpuCreateInstance(addressof(WGPUInstanceDescriptor{
        .nextInChain = nullptr,
    }));
}

void cleanup_context(epix::Res<Context> ctx) {
    wgpuInstanceRelease(ctx->instance);
}

struct TestPlugin : epix::Plugin {
    void build(epix::App& app) override {
        app.add_system(Startup, insert_context, setup_context).chain();
        app.add_system(Exit, cleanup_context);
    }
};

int main() {
    epix::App app = epix::App::create2();
    app.add_plugin(epix::window::WindowPlugin{});
    app.get_plugin<epix::window::WindowPlugin>()
        ->primary_desc()
        .set_size(800, 600)
        .set_vsync(false);
    app.add_plugin(epix::input::InputPlugin{});
    app.add_plugin(TestPlugin{});
    app.run();
}