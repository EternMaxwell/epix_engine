#include <epix/app.h>
#include <epix/input.h>
#include <epix/wgpu.h>
#include <epix/window.h>

using namespace epix;

struct Context {
    wgpu::Instance instance;
    wgpu::Surface surface;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Queue queue;
    wgpu::CommandEncoder encoder;
};

template <typename T>
std::remove_reference_t<T>* addressof(T&& arg) noexcept {
    return reinterpret_cast<std::remove_reference_t<T>*>(
        &const_cast<char&>(reinterpret_cast<const volatile char&>(arg))
    );
}

void insert_context(epix::Command cmd) { cmd.insert_resource(Context{}); }

void setup_context(
    epix::ResMut<Context> ctx,
    Query<Get<window::Window>, With<window::PrimaryWindow>> window
) {
    if (!window) return;
    auto [window] = window.single();
    ctx->instance = wgpu::createInstance(WGPUInstanceDescriptor{
        .nextInChain = nullptr,
    });
    ctx->surface =
        ctx->instance.createSurface(WGPUSurfaceDescriptor{.label = "Surface"});
    ctx->adapter = ctx->instance.requestAdapter(WGPURequestAdapterOptions{});
    ctx->device  = ctx->adapter.requestDevice(WGPUDeviceDescriptor{
         .label = "Device",
         .defaultQueue{
             .label = "Queue",
        },
    });
    ctx->device.setUncapturedErrorCallback([](wgpu::ErrorType type,
                                              const char* message) {
        static auto logger = spdlog::default_logger()->clone("wgpu");
        logger->error("Error: {}", message);
    });
    ctx->queue = ctx->device.getQueue();
    ctx->encoder =
        ctx->device.createCommandEncoder(WGPUCommandEncoderDescriptor{
            .label = "Command Encoder",
        });
}

void submit_test(epix::ResMut<Context> ctx) {
    ctx->encoder.insertDebugMarker("Test");
    wgpu::CommandBuffer commands = ctx->encoder.finish();
    ctx->queue.submit(1, &commands);
    commands.release();
}

void cleanup_context(epix::ResMut<Context> ctx) {
    ctx->queue.release();
    ctx->device.release();
    ctx->adapter.release();
    ctx->instance.release();
}

struct TestPlugin : epix::Plugin {
    void build(epix::App& app) override {
        app.add_system(Startup, insert_context, setup_context).chain();
        app.add_system(Update, submit_test);
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