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
    wgpu::Surface surface   = app.world()
                                  .get_resource<AnonymousSurface>()
                                  .transform([&](const AnonymousSurface& anonymous_surface) -> wgpu::Surface {
                                    return anonymous_surface.create_surface(instance);
                                  })
                                  .value_or(wgpu::Surface{});
    wgpu::Adapter adapter   = instance.requestAdapter(wgpu::RequestAdapterOptions()
                                                          .setCompatibleSurface(surface)
                                                          .setPowerPreference(wgpu::PowerPreference::eHighPerformance)
                                                          .setBackendType(wgpu::BackendType::eVulkan));
    surface                 = nullptr;  // release the temporary surface
    app.world_mut().remove_resource<AnonymousSurface>();
    if (!adapter) {
        throw std::runtime_error("Failed to request WebGPU adapter");
    }
    wgpu::DeviceDescriptor deviceDesc =
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
                }));
    wgpu::Device device = adapter.requestDevice(deviceDesc);
    wgpu::Queue queue   = device.getQueue();
    app.world_mut().insert_resource(instance.clone());
    app.world_mut().insert_resource(adapter.clone());
    app.world_mut().insert_resource(device.clone());
    app.world_mut().insert_resource(queue.clone());
    // keep the device descriptor to make the callbacks alive.
    app.world_mut().insert_resource(std::move(deviceDesc));

    app.sub_app_mut(Render).then([&](App& render_app) {
        render_app.world_mut().insert_resource(instance.clone());
        render_app.world_mut().insert_resource(adapter.clone());
        render_app.world_mut().insert_resource(device.clone());
        render_app.world_mut().insert_resource(queue.clone());
        render_app.world_mut().insert_resource(PipelineServer(device.clone()));
        render_app.add_systems(ExtractSchedule, into(PipelineServer::extract_shaders).set_name("extract shaders"))
            .add_systems(Render, into([](Res<wgpu::Device> device) { device->poll(false); },
                                      [](World& world) { world.clear_entities(); })
                                     .set_names(std::array{"device poll", "clear render entities"})
                                     .after(RenderSet::Cleanup))
            .add_systems(Render, into(PipelineServer::process_pipeline_system, render_system)
                                     .in_set(RenderSet::Render)
                                     .set_names(std::array{"process pipeline", "render system"}))
            .add_systems(Render,
                         into([](ParamSet<World&, ResMut<core::Schedules>> params) {
                             auto&& [world, schedules] = params.get();
                             schedules.get_mut().get_schedule_mut(ExtractSchedule).value().get().apply_deferred(world);
                         })
                             .in_set(RenderSet::PostExtract)
                             .set_name("apply extract deferred"));
    });

    app.add_plugins(render::window::WindowRenderPlugin{});
    app.add_plugins(image::ImagePlugin{});
    app.add_plugins(render::ExtractAssetPlugin<image::Image>{});
    app.add_plugins(render::ShaderPlugin{});
}
void RenderPlugin::finalize(App& app) {}