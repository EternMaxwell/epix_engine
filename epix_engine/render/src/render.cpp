#include "epix/render.h"

EPIX_API void epix::render::RenderPlugin::build(epix::App& app) {
    // create webgpu render resources
    spdlog::info("Creating WebGPU render resources");
    // instance
    wgpu::Instance instance = wgpu::createInstance(WGPUInstanceDescriptor{});
    // create primary window surface for temporary use
    wgpu::Surface primary_surface =
        app.run_system(
               [&](Query<Get<GLFWwindow*>, With<epix::window::PrimaryWindow>>
                       primary_window_query) {
                   return epix::webgpu::utils::create_surface(
                       instance, std::get<0>(primary_window_query.single())
                   );
               }
        ).value();
    wgpu::Adapter adapter = instance.requestAdapter(
        WGPURequestAdapterOptions{.compatibleSurface = primary_surface}
    );
    // create device
    wgpu::Device device = adapter.requestDevice(WGPUDeviceDescriptor{
        .label = {"epix::render::device", WGPU_STRLEN},
        .defaultQueue =
            WGPUQueueDescriptor{
                .label = {"epix::render::queue", WGPU_STRLEN},
            },
        .deviceLostCallbackInfo =
            WGPUDeviceLostCallbackInfo{
                .mode = wgpu::CallbackMode::AllowProcessEvents,
                .callback =
                    [](WGPUDevice const* device, WGPUDeviceLostReason reason,
                       WGPUStringView message, WGPU_NULLABLE void* userdata1,
                       WGPU_NULLABLE void* userdata2) {
                        // Handle device lost
                    },
                .userdata1 = nullptr,
                .userdata2 = nullptr,
            },
        .uncapturedErrorCallbackInfo =
            WGPUUncapturedErrorCallbackInfo{
                .callback =
                    [](const WGPUDevice* device, WGPUErrorType type,
                       WGPUStringView message, void* userdata1,
                       void* userdata2) {
                        // Handle uncaptured error
                    },
                .userdata1 = nullptr,
                .userdata2 = nullptr,
            },
    });
    wgpu::Queue queue   = device.getQueue();
    spdlog::info("Inserting WebGPU render resources into app");

    using namespace epix;
    using namespace epix::app;
    UntypedRes instance_res = UntypedRes::emplace(instance);
    UntypedRes adapter_res  = UntypedRes::emplace(adapter);
    UntypedRes device_res   = UntypedRes::emplace(device);
    UntypedRes queue_res    = UntypedRes::emplace(queue);
    app.add_resource(instance_res);
    app.add_resource(adapter_res);
    app.add_resource(device_res);
    app.add_resource(queue_res);
    auto& render_world = app.world(RenderWorld);
    render_world.add_resource(instance_res);
    render_world.add_resource(adapter_res);
    render_world.add_resource(device_res);
    render_world.add_resource(queue_res);
    primary_surface.release();
    spdlog::info("WebGPU render resources created");

    app.add_systems(
        PostExit,
        into([](Res<wgpu::Device> device, Res<wgpu::Adapter> adapter,
                Res<wgpu::Instance> instance, Res<wgpu::Queue> queue) {
            // Cleanup resources
            device->release();
            adapter->release();
            instance->release();
            queue->release();
        })
    );
    app.add_plugins(epix::render::window::WindowRenderPlugin{});
}