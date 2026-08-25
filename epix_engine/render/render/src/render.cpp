
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

RenderPlugin& RenderPlugin::set_validation(int level) noexcept {
    validation = level;
    return *this;
}

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
    // Honor WGPU_BACKEND / WGPU_POWER_PREF / WGPU_SETTINGS_PRIO (Bevy
    // settings_priority_from_env); the adapter/device creation below consumes
    // the resulting WgpuSettings.
    if (auto* settings = render_creation.automatic_settings()) {
        settings->apply_env_overrides();
    }
    // Bevy lib.rs:382: RenderAssetBytesPerFrame lives in the main world; the
    // limiter + extract/reset systems live in the render app (lib.rs:383-390).
    app.world_mut().init_resource<RenderAssetBytesPerFrame>();
    // Bevy's render-side CameraPlugin requires Msaa directly from Camera.
    // Epix required components are transitive, so Camera2d/Camera3d inherit
    // this one requirement through their Camera requirement.
    app.world_mut().register_required_components_with<::epix::camera::Camera>([] {
        return render::view::Msaa::Sample4;
    });
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
        render_app.world_mut().init_resource<render_resource::TextureCache>();
        render_app.world_mut().init_resource<render::texture::ManualTextureViews>();
        // Render-side camera wiring (bevy_render::camera): extract normalized cameras into
        // the render world, sort them per target, and drive each camera's
        // render graph. The user-facing camera plugin lives in the camera module.
        render_app.world_mut().insert_resource(::epix::camera::ClearColor{});
        render_app.world_mut().init_resource<render::camera::SortedCameras>();
        // Extraction includes per-view HDR/color grading, temporal jitter,
        // exposure, main-pass resolution overrides, and camera-owned
        // main-texture usages.
        // Extraction maps main-world visible entities into their render-world
        // counterparts, extracts `NoIndirectDrawing` from the camera/support
        // state, and carries the independently extracted main-texture usage.
        render_app.add_systems(ExtractSchedule, into(render::camera::extract_cameras).set_name("extract cameras"));
        render_app.add_systems(
            Render, into(render::camera::sort_cameras).in_set(RenderSystems::ManageViews).set_name("sort cameras"));
        render_app.add_systems(Render, into(render_resource::update_texture_cache_system)
                                           .in_set(RenderSystems::Cleanup)
                                           .set_name("update texture cache"));
        if (auto render_graph = render_app.get_resource_mut<graph::RenderGraph>()) {
            render_graph->get().add_node(render::camera::CameraDriverNodeLabel, render::camera::CameraDriverNode{});
        }
    });

    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Queue queue;
    wgpu::Limits limits{};
    RenderAdapterInfo adapter_info;
    std::optional<wgpu::DeviceDescriptor> automatic_device_descriptor;

    if (auto* settings = render_creation.automatic_settings()) {
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
        instance.enumerateAdapters(&adapters[0]);
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
    if (desired_adapter_name.has_value()) {
        std::size_t count = instance.enumerateAdapters(nullptr);
        std::vector<wgpu::Adapter> adapters(count);
        instance.enumerateAdapters(&adapters[0]);
        std::string needle = *desired_adapter_name;
        std::ranges::transform(needle, needle.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (auto& candidate : adapters) {
            wgpu::AdapterInfo info;
            candidate.getInfo(&info);
            std::string device(info.device);
            std::ranges::transform(device, device.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (device.find(needle) != std::string::npos) {
                adapter = candidate;
                break;
            }
        }
    }
    if (!adapter) {
        adapter = instance.requestAdapter(
            wgpu::RequestAdapterOptions()
                .setCompatibleSurface(surface)
                .setPowerPreference(settings->power_preference)
                .setBackendType(settings->backends.value_or(wgpu::BackendType::eUndefined))
                .setForceFallbackAdapter(settings->force_fallback_adapter ? wgpu::Bool(true) : wgpu::Bool(false)));
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

    // Engine-mandatory features plus WgpuSettings::features (Bevy renderer:
    // settings.features | required_features).
    std::vector<wgpu::FeatureName> required_features{
        // NativeFeature::eTextureAdapterSpecificFormatFeatures: exposes
        // per-hardware texture capabilities, including read-write storage
        // access for formats like RGBA8Unorm on Vulkan/DX12/Metal.
        wgpu::FeatureName(wgpu::NativeFeature::eTextureAdapterSpecificFormatFeatures)};
    required_features.insert(required_features.end(), settings->features.begin(), settings->features.end());
    const auto request_optional_native_feature = [&adapter, &required_features](wgpu::NativeFeature feature) {
        const auto named_feature = wgpu::FeatureName(feature);
        if (adapter.hasFeature(named_feature)) required_features.push_back(named_feature);
    };
    const auto request_optional_feature = [&adapter, &required_features](wgpu::FeatureName feature) {
        if (adapter.hasFeature(feature)) required_features.push_back(feature);
    };
    request_optional_feature(wgpu::FeatureName::eIndirectFirstInstance);
    request_optional_native_feature(wgpu::NativeFeature::eSpirvShaderPassthrough);
    request_optional_native_feature(wgpu::NativeFeature::ePushConstants);
    request_optional_native_feature(wgpu::NativeFeature::eMultiDrawIndirect);
    request_optional_native_feature(wgpu::NativeFeature::eMultiDrawIndirectCount);
    request_optional_native_feature(wgpu::NativeFeature::eTextureBindingArray);
    request_optional_native_feature(wgpu::NativeFeature::eStorageResourceBindingArray);
    request_optional_native_feature(wgpu::NativeFeature::eBufferBindingArray);
    automatic_device_descriptor =
        wgpu::DeviceDescriptor()
            .setLabel(wgpu::StringView(settings->device_label))
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
    if (settings->limits.has_value()) {
        automatic_device_descriptor->setRequiredLimits(*settings->limits);
    }
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
    // Bevy's default image sampler is ImageSamplerDescriptor::linear()

    // (bevy_image image.rs:189, 806-813): linear min/mag/mipmap filters,
    // clamp-to-edge addressing, lod clamp 0..32.
    wgpu::Sampler default_sampler = device.createSampler(wgpu::SamplerDescriptor()
                                                             .setLabel("DefaultImageSampler")
                                                             .setAddressModeU(wgpu::AddressMode::eClampToEdge)
                                                             .setAddressModeV(wgpu::AddressMode::eClampToEdge)
                                                             .setAddressModeW(wgpu::AddressMode::eClampToEdge)
                                                             .setMinFilter(wgpu::FilterMode::eLinear)
                                                             .setMagFilter(wgpu::FilterMode::eLinear)
                                                             .setMipmapFilter(wgpu::MipmapFilterMode::eLinear)
                                                             .setLodMinClamp(0.0f)
                                                             .setLodMaxClamp(32.0f)
                                                             .setMaxAnisotropy(1));
    app.world_mut().insert_resource(render::DefaultImageSampler{
        .sampler = default_sampler,
    });
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
        render_app.world_mut().insert_resource(render::DefaultImageSampler{
            .sampler = default_sampler,
        });
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
    app.add_plugins(render::RenderAssetPlugin<image::Image>{});
    app.add_plugins(shader::ShaderPlugin{});
    // CameraPlugin supplies Camera's required RenderTarget component before
    // the independently extracted camera components are installed.
    app.add_plugins(::epix::camera::CameraPlugin{});
    // ManualTextureViews is owned by applications in the main world and
    // extracted for target preparation.  It must be available there as well
    // as in the render world so camera projection updates can resolve its
    // physical size, as Bevy's render-side camera_system does.
    app.world_mut().init_resource<render::texture::ManualTextureViews>();
    app.add_plugins(render::ExtractResourcePlugin<render::texture::ManualTextureViews>{});
    app.add_systems(app::PostStartup,
                    into(render::view::update_manual_texture_view_cameras<::epix::camera::Projection>)
                        .after(::epix::camera::CameraUpdateSystems::CameraUpdateSystem)
                        .set_name("startup update manual texture view cameras"));
    app.add_systems(app::PostStartup,
                    into(render::view::update_manual_texture_view_cameras<::epix::camera::OrthographicProjection>)
                        .after(::epix::camera::CameraUpdateSystems::CameraUpdateSystem)
                        .set_name("startup update manual texture view orthographic cameras"));
    app.add_systems(app::PostStartup,
                    into(render::view::update_manual_texture_view_cameras<::epix::camera::PerspectiveProjection>)
                        .after(::epix::camera::CameraUpdateSystems::CameraUpdateSystem)
                        .set_name("startup update manual texture view perspective cameras"));
    app.add_systems(app::PostUpdate,
                    into(render::view::update_manual_texture_view_cameras<::epix::camera::Projection>)
                        .after(::epix::camera::CameraUpdateSystems::CameraUpdateSystem)
                        .set_name("update manual texture view cameras"));
    app.add_systems(app::PostUpdate,
                    into(render::view::update_manual_texture_view_cameras<::epix::camera::OrthographicProjection>)
                        .after(::epix::camera::CameraUpdateSystems::CameraUpdateSystem)
                        .set_name("update manual texture view orthographic cameras"));
    app.add_systems(app::PostUpdate,
                    into(render::view::update_manual_texture_view_cameras<::epix::camera::PerspectiveProjection>)
                        .after(::epix::camera::CameraUpdateSystems::CameraUpdateSystem)
                        .set_name("update manual texture view perspective cameras"));
    // Bevy bevy_render extracts the ClearColor resource to the render world.
    app.add_plugins(render::ExtractResourcePlugin<::epix::camera::ClearColor>{});
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
