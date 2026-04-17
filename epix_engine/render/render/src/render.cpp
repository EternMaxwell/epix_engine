module;

#include <spdlog/spdlog.h>

// include header to deal with partial specialization problem in MSVC
#include <format>
#include <stacktrace>

module epix.render;

import webgpu;
import std;

using namespace epix::render;
using namespace epix::core;

RenderPlugin& RenderPlugin::set_validation(int level) {
    validation = level;
    return *this;
}

void epix::render::render_system(World& world) {
    auto&& graph  = world.resource_mut<graph::RenderGraph>();
    auto&& device = world.resource<wgpu::Device>();
    auto&& queue  = world.resource<wgpu::Queue>();
    graph.update(world);
    graph::RenderGraphRunner::run(graph, device, queue, world, {});
}

void RenderPlugin::build(App& app) {
    spdlog::debug("[render] Building RenderPlugin.");
    app.add_sub_app(Render);
    app.sub_app_mut(Render).then([](App& render_app) {
        render_app
            .add_schedule(Schedule(render::ExtractSchedule)
                              .with_schedule_config(core::ScheduleConfig{
                                  .executor_config = {.deferred = core::DeferredApply::Ignore},
                              }))
            .add_schedule(render::Render.render_schedule())
            .set_extract_fn([](App& render_app, World& main_world) { render_app.run_schedule(ExtractSchedule); });
        render_app.schedule_order().insert_begin(render::Render);
        render_app.world_mut().emplace_resource<graph::RenderGraph>();
    });

    wgpu::Instance instance = wgpu::createInstance();
    spdlog::debug("[render] WebGPU instance created.");
    wgpu::Surface surface = app.world()
                                .get_resource<AnonymousSurface>()
                                .transform([&](const AnonymousSurface& anonymous_surface) -> wgpu::Surface {
                                    return anonymous_surface.create_surface(instance);
                                })
                                .value_or(wgpu::Surface{});
    wgpu::Adapter adapter = instance.requestAdapter(wgpu::RequestAdapterOptions()
                                                        .setCompatibleSurface(surface)
                                                        .setPowerPreference(wgpu::PowerPreference::eHighPerformance)
                                                        .setBackendType(wgpu::BackendType::eVulkan));
    surface               = nullptr;  // release the temporary surface
    app.world_mut().remove_resource<AnonymousSurface>();
    if (!adapter) {
        throw std::runtime_error("Failed to request WebGPU adapter");
    }
    // show info about acquired adapter
    {
        wgpu::AdapterInfo adapterInfo;
        adapter.getInfo(&adapterInfo);
        spdlog::info("[render] Acquired WebGPU adapter: vender={}, architecture={}, device={}, description={}",
                     std::string_view(adapterInfo.vendor), std::string_view(adapterInfo.architecture),
                     std::string_view(adapterInfo.device), std::string_view(adapterInfo.description));
    }

    wgpu::DeviceDescriptor deviceDesc =
        wgpu::DeviceDescriptor()
            .setLabel("Render Device")
            .setDefaultQueue(wgpu::QueueDescriptor().setLabel("Render Queue"))
            // NativeFeature::eTextureAdapterSpecificFormatFeatures:
            // exposes per-hardware texture capabilities, including read-write
            // storage access for formats like RGBA8Unorm on Vulkan/DX12/Metal.
            .setRequiredFeatures(
                std::array{wgpu::FeatureName(wgpu::NativeFeature::eTextureAdapterSpecificFormatFeatures),
                           wgpu::FeatureName(wgpu::NativeFeature::eSpirvShaderPassthrough)})
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
    spdlog::debug("[render] WebGPU device created.");
    wgpu::Limits limits;
    device.getLimits(&limits);
    wgpu::Queue queue = device.getQueue();
    app.world_mut().insert_resource(instance.clone());
    app.world_mut().insert_resource(adapter.clone());
    app.world_mut().insert_resource(device.clone());
    app.world_mut().insert_resource(queue.clone());
    app.world_mut().insert_resource(limits);
    wgpu::Sampler default_sampler = device.createSampler(wgpu::SamplerDescriptor()
                                                             .setLabel("DefaultImageSampler")
                                                             .setAddressModeU(wgpu::AddressMode::eClampToEdge)
                                                             .setAddressModeV(wgpu::AddressMode::eClampToEdge)
                                                             .setAddressModeW(wgpu::AddressMode::eClampToEdge)
                                                             .setMinFilter(wgpu::FilterMode::eNearest)
                                                             .setMagFilter(wgpu::FilterMode::eNearest)
                                                             .setMipmapFilter(wgpu::MipmapFilterMode::eNearest)
                                                             .setLodMinClamp(0.0f)
                                                             .setLodMaxClamp(0.0f)
                                                             .setMaxAnisotropy(1));
    app.world_mut().insert_resource(render::DefaultImageSampler{
        .sampler = default_sampler,
    });
    // keep the device descriptor to make the callbacks alive.
    app.world_mut().insert_resource(std::move(deviceDesc));

    app.sub_app_mut(Render).then([&](App& render_app) {
        render_app.world_mut().insert_resource(instance.clone());
        render_app.world_mut().insert_resource(adapter.clone());
        render_app.world_mut().insert_resource(device.clone());
        render_app.world_mut().insert_resource(queue.clone());
        render_app.world_mut().insert_resource(limits);
        render_app.world_mut().insert_resource(render::DefaultImageSampler{
            .sampler = default_sampler,
        });
        PipelineServer pipeline_server(device.clone());
        app.world_mut().insert_resource(pipeline_server);
        render_app.world_mut().insert_resource(std::move(pipeline_server));
        render_app
            .add_systems(ExtractSchedule, into(PipelineServer::extract_shaders, PipelineServer::process_pipeline_system)
                                              .chain()
                                              .set_names(std::array{"extract shaders", "process pipeline"}))
            .add_systems(Render, into([](Res<wgpu::Device> device) { device->poll(false); },
                                      [](World& world) { world.clear_entities(); })
                                     .set_names(std::array{"device poll", "clear render entities"})
                                     .after(RenderSet::Cleanup))
            .add_systems(Render, into(render_system).in_set(RenderSet::Render).set_name("render system"))
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
    app.add_plugins(shader::ShaderPlugin{});
    app.add_plugins(render::camera::CameraPlugin{});
    app.add_plugins(render::view::ViewPlugin{});
}
void RenderPlugin::finalize(App& app) {}
