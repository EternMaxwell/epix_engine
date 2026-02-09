module;

#include <spdlog/spdlog.h>

// include header to deal with partial specialization problem in MSVC
#include <format>
#include <stacktrace>

module epix.render;

import webgpu;
import std;

using namespace render;
using namespace core;

RenderPlugin& RenderPlugin::set_validation(int level) {
    validation = level;
    return *this;
}

void render::render_system(World& world) {
    auto&& graph  = world.resource_mut<graph::RenderGraph>();
    auto&& device = world.resource<wgpu::Device>();
    auto&& queue  = world.resource<wgpu::Queue>();
    graph.update(world);
    graph::RenderGraphRunner::run(graph, device, queue, world, {});
}

void RenderPlugin::build(App& app) {
    app.add_sub_app(Render);
    app.sub_app_mut(Render).then([](App& render_app) {
        render_app
            .add_schedule(Schedule(render::ExtractSchedule)
                              .with_execute_config(core::ExecuteConfig{
                                  .handle_deferred = false,
                              }))
            .add_schedule(render::Render.render_schedule())
            .set_extract_fn([](App& render_app, World& main_world) { render_app.run_schedule(ExtractSchedule); });
        render_app.schedule_order().insert_begin(render::Render);
        render_app.world_mut().emplace_resource<graph::RenderGraph>();
    });

    wgpu::Instance instance = wgpu::createInstance();
    auto& anonymous_surface = app.world().resource<AnonymousSurface>();
    if (!anonymous_surface.create_surface) {
        throw std::runtime_error(
            "AnonymousSurface resource must have a valid create_surface function before building RenderPlugin");
    }
    wgpu::Surface surface = anonymous_surface.create_surface(instance);
    wgpu::Adapter adapter = instance.requestAdapter(
        wgpu::RequestAdapterOptions().setCompatibleSurface(surface).setBackendType(wgpu::BackendType::eVulkan));
    surface = nullptr;  // release the temporary surface
    app.world_mut().remove_resource<AnonymousSurface>();
    if (!adapter) {
        throw std::runtime_error("Failed to request WebGPU adapter");
    }
    wgpu::Device device = adapter.requestDevice(
        wgpu::DeviceDescriptor()
            .setLabel("Render Device")
            .setDefaultQueue(wgpu::QueueDescriptor().setLabel("Render Queue"))
            .setDeviceLostCallbackInfo(wgpu::DeviceLostCallbackInfo().setCallback(
                [](wgpu::Device const& device, wgpu::DeviceLostReason reason, wgpu::StringView message) {
                    std::stacktrace stack = std::stacktrace::current();
                    spdlog::error("WebGPU Device lost: {}, with stack:\n{}", std::string_view(message), stack);
                }))
            .setUncapturedErrorCallbackInfo(wgpu::UncapturedErrorCallbackInfo().setCallback(
                [](wgpu::Device const& device, wgpu::ErrorType type, wgpu::StringView message) {
                    std::stacktrace stack = std::stacktrace::current();
                    spdlog::error("WebGPU Uncaptured error: {}, with stack:\n{}", std::string_view(message), stack);
                })));
    wgpu::Queue queue = device.getQueue();
    app.world_mut().insert_resource(instance.clone());
    app.world_mut().insert_resource(adapter.clone());
    app.world_mut().insert_resource(device.clone());
    app.world_mut().insert_resource(queue.clone());

    app.sub_app_mut(Render).then([&](App& render_app) {
        render_app.world_mut().insert_resource(instance.clone());
        render_app.world_mut().insert_resource(adapter.clone());
        render_app.world_mut().insert_resource(device.clone());
        render_app.world_mut().insert_resource(queue.clone());
        render_app
            .add_systems(Render, into([](Res<wgpu::Device> device) { device->poll(false); },
                                      [](World& world) { world.clear_entities(); })
                                     .set_names(std::array{"device poll", "clear render entities"})
                                     .after(RenderSet::Cleanup))
            .add_systems(Render, into(render_system).in_set(RenderSet::Render).set_names(std::array{"render system"}))
            .add_systems(Render,
                         into([](ParamSet<World&, ResMut<core::Schedules>> params) {
                             auto&& [world, schedules] = params.get();
                             schedules.get_mut().get_schedule_mut(ExtractSchedule).value().get().apply_deferred(world);
                         })
                             .in_set(RenderSet::PostExtract)
                             .set_name("apply extract deferred"));
    });
}
void RenderPlugin::finalize(App& app) {}