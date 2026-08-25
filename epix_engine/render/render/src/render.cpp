
#include <spdlog/spdlog.h>

// include header to deal with partial specialization problem in MSVC
#include <algorithm>
#include <cctype>
#include <epix/render.hpp>
#include <format>
#include <memory>
#include <stacktrace>
#include <vector>
#include <webgpu/webgpu.hpp>

using namespace epix::render;
using namespace epix::ecs;
using namespace epix::app;

namespace {

wgpu::Limits constrain_limits(wgpu::Limits limits, const wgpu::Limits& constraints) {
    const auto upper_bound = [](auto& limit, const auto constraint) { limit = std::min(limit, constraint); };
    const auto lower_bound = [](auto& limit, const auto constraint) { limit = std::max(limit, constraint); };
    upper_bound(limits.maxTextureDimension1D, constraints.maxTextureDimension1D);
    upper_bound(limits.maxTextureDimension2D, constraints.maxTextureDimension2D);
    upper_bound(limits.maxTextureDimension3D, constraints.maxTextureDimension3D);
    upper_bound(limits.maxTextureArrayLayers, constraints.maxTextureArrayLayers);
    upper_bound(limits.maxBindGroups, constraints.maxBindGroups);
    upper_bound(limits.maxBindGroupsPlusVertexBuffers, constraints.maxBindGroupsPlusVertexBuffers);
    upper_bound(limits.maxBindingsPerBindGroup, constraints.maxBindingsPerBindGroup);
    upper_bound(limits.maxDynamicUniformBuffersPerPipelineLayout, constraints.maxDynamicUniformBuffersPerPipelineLayout);
    upper_bound(limits.maxDynamicStorageBuffersPerPipelineLayout, constraints.maxDynamicStorageBuffersPerPipelineLayout);
    upper_bound(limits.maxSampledTexturesPerShaderStage, constraints.maxSampledTexturesPerShaderStage);
    upper_bound(limits.maxSamplersPerShaderStage, constraints.maxSamplersPerShaderStage);
    upper_bound(limits.maxStorageBuffersPerShaderStage, constraints.maxStorageBuffersPerShaderStage);
    upper_bound(limits.maxStorageTexturesPerShaderStage, constraints.maxStorageTexturesPerShaderStage);
    upper_bound(limits.maxUniformBuffersPerShaderStage, constraints.maxUniformBuffersPerShaderStage);
    upper_bound(limits.maxUniformBufferBindingSize, constraints.maxUniformBufferBindingSize);
    upper_bound(limits.maxStorageBufferBindingSize, constraints.maxStorageBufferBindingSize);
    lower_bound(limits.minUniformBufferOffsetAlignment, constraints.minUniformBufferOffsetAlignment);
    lower_bound(limits.minStorageBufferOffsetAlignment, constraints.minStorageBufferOffsetAlignment);
    upper_bound(limits.maxVertexBuffers, constraints.maxVertexBuffers);
    upper_bound(limits.maxBufferSize, constraints.maxBufferSize);
    upper_bound(limits.maxVertexAttributes, constraints.maxVertexAttributes);
    upper_bound(limits.maxVertexBufferArrayStride, constraints.maxVertexBufferArrayStride);
    upper_bound(limits.maxInterStageShaderVariables, constraints.maxInterStageShaderVariables);
    upper_bound(limits.maxColorAttachments, constraints.maxColorAttachments);
    upper_bound(limits.maxColorAttachmentBytesPerSample, constraints.maxColorAttachmentBytesPerSample);
    upper_bound(limits.maxComputeWorkgroupStorageSize, constraints.maxComputeWorkgroupStorageSize);
    upper_bound(limits.maxComputeInvocationsPerWorkgroup, constraints.maxComputeInvocationsPerWorkgroup);
    upper_bound(limits.maxComputeWorkgroupSizeX, constraints.maxComputeWorkgroupSizeX);
    upper_bound(limits.maxComputeWorkgroupSizeY, constraints.maxComputeWorkgroupSizeY);
    upper_bound(limits.maxComputeWorkgroupSizeZ, constraints.maxComputeWorkgroupSizeZ);
    upper_bound(limits.maxComputeWorkgroupsPerDimension, constraints.maxComputeWorkgroupsPerDimension);
    return limits;
}

} // namespace

RenderAdapterInfo RenderAdapterInfo::from_adapter(const wgpu::Adapter& adapter) {
    wgpu::AdapterInfo native;
    adapter.getInfo(&native);
    return RenderAdapterInfo{
        .vendor       = std::string(native.vendor),
        .architecture = std::string(native.architecture),
        .device       = std::string(native.device),
        .description  = std::string(native.description),
        .backend_type = native.backendType,
        .adapter_type = native.adapterType,
        .vendor_id    = native.vendorID,
        .device_id    = native.deviceID,
    };
}

std::optional<WgpuSettingsPriority> epix::render::settings_priority_from_env() noexcept {
    const char* setting = std::getenv("WGPU_SETTINGS_PRIO");
    if (!setting) return std::nullopt;
    std::string value(setting);
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (value == "compatibility") return WgpuSettingsPriority::Compatibility;
    if (value == "functionality") return WgpuSettingsPriority::Functionality;
    if (value == "webgl2") return WgpuSettingsPriority::WebGL2;
    return std::nullopt;
}

Backends Backends::from_comma_list(std::string_view value) noexcept {
    Backends result = Backends::empty();
    while (!value.empty()) {
        const auto comma = value.find(',');
        auto name        = value.substr(0, comma);
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.remove_prefix(1);
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.remove_suffix(1);
        std::string normalized(name);
        std::ranges::transform(normalized, normalized.begin(),
                               [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        if (normalized == "noop") result = result | Noop;
        else if (normalized == "vulkan" || normalized == "vk") result = result | Vulkan;
        else if (normalized == "metal" || normalized == "mtl") result = result | Metal;
        else if (normalized == "dx12" || normalized == "d3d12") result = result | Dx12;
        else if (normalized == "opengl" || normalized == "gles" || normalized == "gl") result = result | Gl;
        else if (normalized == "webgpu") result = result | BrowserWebGpu;
        if (comma == std::string_view::npos) break;
        value.remove_prefix(comma + 1);
    }
    return result;
}

std::optional<Backends> Backends::from_env() noexcept {
    const char* value = std::getenv("WGPU_BACKEND");
    if (!value) return std::nullopt;
    return from_comma_list(value);
}

std::optional<std::uint32_t> epix::render::get_adreno_model(const RenderAdapterInfo& adapter_info) noexcept {
#if defined(__ANDROID__)
    constexpr std::string_view prefix = "Adreno (TM) ";
    const std::string_view name = adapter_info.device;
    if (!name.starts_with(prefix)) return std::nullopt;
    std::uint32_t value = 0;
    bool found_digit = false;
    for (const char character : name.substr(prefix.size())) {
        if (character < '0' || character > '9') break;
        found_digit = true;
        value = value * 10u + static_cast<std::uint32_t>(character - '0');
    }
    return found_digit ? std::optional<std::uint32_t>{value} : std::nullopt;
#else
    (void)adapter_info;
    return std::nullopt;
#endif
}

std::optional<std::uint32_t> epix::render::get_mali_driver_version(const RenderAdapterInfo& adapter_info) noexcept {
#if defined(__ANDROID__)
    if (!adapter_info.device.contains("Mali")) return std::nullopt;
    constexpr std::string_view prefix = "v1.r";
    const auto start = adapter_info.description.find(prefix);
    if (start == std::string::npos) return std::nullopt;
    const auto end = adapter_info.description.find('p', start + prefix.size());
    if (end == std::string::npos) return std::nullopt;
    std::uint32_t value = 0;
    const auto digits = std::string_view(adapter_info.description).substr(start + prefix.size(), end - start - prefix.size());
    if (digits.empty()) return std::nullopt;
    for (const char character : digits) {
        if (character < '0' || character > '9') return std::nullopt;
        value = value * 10u + static_cast<std::uint32_t>(character - '0');
    }
    return value;
#else
    (void)adapter_info;
    return std::nullopt;
#endif
}

void epix::render::render_system(World& world) {
    auto&& graph  = world.resource_mut<graph::RenderGraph>();
    auto&& device = world.resource<wgpu::Device>();
    auto&& queue  = world.resource<wgpu::Queue>();
    graph.update(world);
    // Bevy render_system (renderer/mod.rs:82-89): after the graph runs, record
    // the readback copy commands into the same encoder before submitting.
    const auto result = graph::RenderGraphRunner::run(graph, device, queue, world, [&world](wgpu::CommandEncoder& encoder) {
        // Optional render extensions append their work after graph output is
        // complete.  Readback copies deliberately run last, so extensions can
        // enqueue shared GpuReadbacks for this same submission.
        if (auto finalizers = world.get_resource<graph::RenderGraphFinalizers>()) {
            for (auto& callback : finalizers->get().callbacks) callback(world, encoder);
        }
        submit_readback_commands(world, encoder);
    });
    if (!result) {
        // Match Bevy's render_system: the encoder is not submitted and this
        // typed failure reaches the application's terminate handler rather
        // than being silently converted into a stale frame.
        throw std::runtime_error(std::format("Render graph failed to run: {}", result.error().to_string()));
    }
}

void RenderPlugin::attach(App& app) {
    spdlog::debug("[render] Attaching RenderPlugin.");
    // Bevy lib.rs:382: RenderAssetBytesPerFrame lives in the main world; the
    // limiter + extract/reset systems live in the render app (lib.rs:383-390).
    app.world_mut().init_resource<RenderAssetBytesPerFrame>();
    app.add_sub_app(Render);
    app.sub_app_mut(Render).then([](App& render_app) {
        // Bevy's extract closure (lib.rs:506-523): run RenderStartup once,
        // then entity_sync_system, then ExtractSchedule. The flag is captured
        // through a shared_ptr so it survives across extract calls.
        auto should_run_startup = std::make_shared<bool>(true);
        // AssetExtractionSystems must be registered in the ExtractSchedule for
        // the extract_*_asset systems' in_set hierarchy (Bevy configure_sets).
        auto extract_schedule = Schedule(render::ExtractSchedule)
                                    .with_schedule_config(ecs::ScheduleConfig{
                                        .executor_config = {.deferred = ecs::DeferredApply::Ignore},
                                    });
        extract_schedule.configure_sets(ecs::sets(AssetExtractionSystems{}));
        render_app.add_schedule(std::move(extract_schedule))
            .add_schedule(Schedule(render::RenderStartup))
            .add_schedule(render::Render.render_schedule())
            .set_extract_fn([should_run_startup](App& render_app, World& main_world) {
                if (*should_run_startup) {
                    render_app.run_schedule(RenderStartup);
                    *should_run_startup = false;
                }
                sync_world::entity_sync_system(main_world, render_app.world_mut());
                render_app.run_schedule(ExtractSchedule);
            });
        render_app.schedule_order().insert_begin(render::Render);
        render_app.world_mut().emplace_resource<graph::RenderGraph>();
        render_app.add_systems(Render, into(render_resource::update_texture_cache_system)
                                           .in_set(RenderSystems::Cleanup)
                                           .set_name("update texture cache"));
    });

    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Queue queue;
    wgpu::Limits limits{};
    RenderAdapterInfo adapter_info;
    std::optional<wgpu::DeviceDescriptor> automatic_device_descriptor;

    if (auto* settings = render_creation.automatic_settings()) {
        // TEMPORARY wgpu-native workaround: this vendored runtime corrupts an
        // InstanceExtras chain when an instance owns a window surface. Keep
        // WgpuSettings Bevy-shaped, but construct the native instance with
        // its defaults until that runtime bug is fixed. The renderer-local
        // Vulkan adapter coercion below is still what preserves Slang's
        // SPIR-V passthrough requirement.
        instance = wgpu::createInstance();
        spdlog::debug("[render] WebGPU instance created.");
    wgpu::Surface surface = app.world()
                                .get_resource<AnonymousSurface>()
                                .transform([&](const AnonymousSurface& anonymous_surface) -> wgpu::Surface {
                                    return anonymous_surface.create_surface(instance);
                                })
                                .value_or(wgpu::Surface{});
    if (auto print_adapters = std::getenv("EPIX_PRINT_WEBGPU_ADAPTERS"); print_adapters && print_adapters[0] != '0') {
        // enumerate all adapters
        std::size_t count = instance.enumerateAdapters(nullptr);
        std::vector<wgpu::Adapter> adapters(count);
        if (count != 0) instance.enumerateAdapters(adapters.data());
        spdlog::info("[render] Available WebGPU adapters:");
        for (const auto& adapter : adapters) {
            wgpu::AdapterInfo adapterInfo;
            adapter.getInfo(&adapterInfo);
            spdlog::info("  vender={}, architecture={}, device={}, description={}",
                         std::string_view(adapterInfo.vendor), std::string_view(adapterInfo.architecture),
                         std::string_view(adapterInfo.device), std::string_view(adapterInfo.description));
        }
    }
    // Adapter selection honors WgpuSettings (Bevy renderer/mod.rs:243-279):
    // WGPU_ADAPTER_NAME env > settings.adapter_name -> enumerate + substring
    // match; otherwise requestAdapter with the configured power preference,
    // backend and fallback flag.
    std::optional<std::string> desired_adapter_name = settings->adapter_name;
    if (const char* env_name = std::getenv("WGPU_ADAPTER_NAME"); env_name && env_name[0] != '\0') {
        desired_adapter_name = std::string(env_name);
    }
    const bool force_fallback_adapter = [] (bool configured) {
        const char* value = std::getenv("WGPU_FORCE_FALLBACK_ADAPTER");
        if (!value) return configured;
        const std::string_view override_value(value);
        // Mirrors Bevy: every non-empty value other than `0` and `false`
        // enables fallback-adapter selection.
        return !(override_value.empty() || override_value == "0" || override_value == "false");
    }(settings->force_fallback_adapter);
    if (desired_adapter_name.has_value()) {
        std::size_t count = instance.enumerateAdapters(nullptr);
        std::vector<wgpu::Adapter> adapters(count);
        if (count != 0) instance.enumerateAdapters(adapters.data());
        std::string needle = *desired_adapter_name;
        std::ranges::transform(needle, needle.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (auto& candidate : adapters) {
            wgpu::AdapterInfo info;
            candidate.getInfo(&info);
            // TEMPORARY: named-adapter selection must obey the same Vulkan
            // coercion as the automatic path while Slang needs native SPIR-V
            // passthrough. Remove this filter with that workaround.
            if (info.backendType != wgpu::BackendType::eVulkan) continue;
            std::string device(info.device);
            std::ranges::transform(device, device.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (device.find(needle) != std::string::npos) {
                adapter = candidate;
                break;
            }
        }
    }
    // TEMPORARY Slang compatibility requirement: authored Slang is compiled
    // to SPIR-V and wgpu-native must pass it through to Vulkan. Keep this
    // renderer-local coercion out of WgpuSettings so its public default stays
    // Bevy-compatible; remove it when native SPIR-V ingestion is fixed.
    const auto automatic_backend = wgpu::BackendType::eVulkan;
    if (!adapter) {
        adapter = instance.requestAdapter(
            wgpu::RequestAdapterOptions()
                .setCompatibleSurface(surface)
                .setPowerPreference(settings->power_preference)
                .setBackendType(automatic_backend)
                .setForceFallbackAdapter(force_fallback_adapter ? wgpu::Bool(true) : wgpu::Bool(false)));
    }
    surface = nullptr;  // release the temporary surface
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
    adapter_info = RenderAdapterInfo::from_adapter(adapter);

    // Bevy's Functionality priority starts from every feature/limit supported
    // by the selected adapter. Compatibility starts from the WebGPU defaults.
    std::vector<wgpu::FeatureName> required_features;
    wgpu::Limits required_limits = settings->limits;
    if (settings->priority == WgpuSettingsPriority::Functionality) {
        wgpu::SupportedFeatures supported_features;
        adapter.getFeatures(&supported_features);
        required_features.assign(supported_features.features.begin(), supported_features.features.end());
        if (adapter_info.adapter_type == wgpu::AdapterType::eDiscreteGPU) {
            std::erase(required_features, wgpu::FeatureName(wgpu::NativeFeature::eMappablePrimaryBuffers));
        }
        wgpu::Limits supported_limits;
        if (adapter.getLimits(&supported_limits) == wgpu::Status::eSuccess) required_limits = supported_limits;
    }
    if (settings->disabled_features.has_value()) {
        for (const auto feature : *settings->disabled_features) std::erase(required_features, feature);
    }
    // Apply configured features after disabled_features, as Bevy does. Its
    // default is TextureAdapterSpecificFormatFeatures, which intentionally
    // remains enabled even when that feature appears in disabled_features.
    required_features.insert(required_features.end(), settings->features.begin(), settings->features.end());
    // TEMPORARY Slang compatibility requirement; remove alongside the Vulkan
    // restriction once wgpu-native can reliably ingest Slang-produced SPIR-V
    // through Naga.
    required_features.push_back(wgpu::FeatureName(wgpu::NativeFeature::eSpirvShaderPassthrough));
    std::ranges::sort(required_features, {}, [](const wgpu::FeatureName feature) { return static_cast<std::uint32_t>(feature); });
    required_features.erase(std::unique(required_features.begin(), required_features.end()), required_features.end());
    if (settings->constrained_limits.has_value()) {
        required_limits = constrain_limits(required_limits, *settings->constrained_limits);
    }
    automatic_device_descriptor =
        wgpu::DeviceDescriptor()
            .setDefaultQueue(wgpu::QueueDescriptor().setLabel("Render Queue"))
            .setRequiredFeatures(required_features)
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
    if (settings->device_label.has_value()) {
        automatic_device_descriptor->setLabel(wgpu::StringView(*settings->device_label));
    }
    automatic_device_descriptor->setRequiredLimits(required_limits);
    device = adapter.requestDevice(*automatic_device_descriptor);
    spdlog::debug("[render] WebGPU device created.");
    device.getLimits(&limits);
    queue = device.getQueue();
    } else {
        const auto& resources = *render_creation.manual_resources();
        if (!resources.instance || !resources.adapter || !resources.device || !resources.queue) {
            throw std::runtime_error("RenderCreation::manual requires non-null instance, adapter, device, and queue");
        }
        instance = resources.instance.clone();
        adapter  = resources.adapter.clone();
        device   = resources.device.clone();
        queue    = resources.queue.clone();
        device.getLimits(&limits);
        adapter_info = resources.adapter_info.device.empty() ? RenderAdapterInfo::from_adapter(adapter) : resources.adapter_info;
        app.world_mut().remove_resource<AnonymousSurface>();
        spdlog::debug("[render] Using manually supplied WebGPU resources.");
    }
    app.world_mut().insert_resource(instance.clone());
    app.world_mut().insert_resource(adapter.clone());
    app.world_mut().insert_resource(adapter_info);
    app.world_mut().insert_resource(device.clone());
    app.world_mut().insert_resource(queue.clone());
    app.world_mut().insert_resource(limits);
    // Keep the automatic descriptor alive because it owns the callback
    // storage. For manual creation, the embedding application owns it.
    if (automatic_device_descriptor) {
        app.world_mut().insert_resource(std::move(*automatic_device_descriptor));
    }

    app.sub_app_mut(Render).then([&](App& render_app) {
        render_app.world_mut().insert_resource(instance.clone());
        render_app.world_mut().insert_resource(adapter.clone());
        render_app.world_mut().insert_resource(adapter_info);
        render_app.world_mut().insert_resource(device.clone());
        render_app.world_mut().insert_resource(queue.clone());
        render_app.world_mut().insert_resource(limits);
        render_app.world_mut().init_resource<graph::RenderGraphFinalizers>();
        // Bevy lib.rs:384: RenderAssetBytesPerFrameLimiter is a render-app
        // resource (required by prepare_assets / extract/reset systems).
        render_app.world_mut().init_resource<RenderAssetBytesPerFrameLimiter>();
        PipelineServer pipeline_server(device.clone(), synchronous_pipeline_compilation);
        app.world_mut().insert_resource(pipeline_server);
        render_app.world_mut().insert_resource(std::move(pipeline_server));
        render_app
            // Bevy lib.rs:383-390: the byte limiter is initialized in the render
            // app; extract_render_asset_bytes_per_frame runs in ExtractSchedule,
            // reset_render_asset_bytes_per_frame in RenderSystems::Cleanup.
            .add_systems(ExtractSchedule,
                         into(extract_render_asset_bytes_per_frame).set_name("extract render asset bytes per frame"))
            .add_systems(Render, into(reset_render_asset_bytes_per_frame)
                                     .in_set(RenderSystems::Cleanup)
                                     .set_name("reset render asset bytes per frame"))
            // Bevy lib.rs:488: only PipelineCache::extract_shaders runs in the
            // ExtractSchedule; process_pipeline_queue_system is chained with
            // render_system in RenderSystems::Render (lib.rs:495-497).
            .add_systems(ExtractSchedule, into(PipelineServer::extract_shaders).set_name("extract shaders"))
            .add_systems(Render, into([](Res<wgpu::Device> device) { device->poll(false); })
                                     .set_name("device poll")
                                     .after(RenderSystems::Cleanup))
            .add_systems(Render, into(PipelineServer::process_pipeline_system, render_system)
                                     .chain()
                                     .in_set(RenderSystems::Render)
                                     .set_names(std::array{"process pipeline", "render system"}))
            .add_systems(Render,
                         into([](ParamSet<World&, ResMut<ecs::Schedules>> params) {
                             auto&& [world, schedules] = params.get();
                             schedules.get_mut().get_schedule_mut(ExtractSchedule).value().get().apply_deferred(world);
                         })
                             .in_set(RenderSystems::ExtractCommands)
                             .set_name("apply extract commands"));
    });

    app.add_plugins(render::window::WindowRenderPlugin{});
    app.add_plugins(image::ImagePlugin{});
    app.add_plugins(render::TexturePlugin{});
    app.add_plugins(shader::ShaderPlugin{});
    // CameraPlugin supplies Camera's required RenderTarget component before
    // the independently extracted camera components are installed.
    app.add_plugins(::epix::camera::CameraPlugin{});
    // Distinct from the camera-module plugin above, this is Bevy's
    // render::camera::CameraPlugin. Add it through the plugin list just as
    // Bevy RenderPlugin does, after the render sub-app is initialized.
    app.add_plugins(render::camera::CameraPlugin{});
    app.add_plugins(render::experimental::OcclusionCullingPlugin{});
    app.add_plugins(render::view::ViewPlugin{});
    // Bevy lib.rs:362-380: GlobalsPlugin, BatchingPlugin, SyncWorldPlugin,
    // StoragePlugin, GpuReadbackPlugin are all attached by RenderPlugin.
    app.add_plugins(render::GlobalsPlugin{});
    app.add_plugins(render::batching::BatchingPlugin{debug_flags});
    app.add_plugins(sync_world::SyncWorldPlugin{});
    app.add_plugins(render::StoragePlugin{});
    app.add_plugins(render::GpuReadbackPlugin{});
}
void RenderPlugin::detach(App& app) noexcept {}
