#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <cstdlib>
#include <epix/app.hpp>
#include <epix/ecs.hpp>
#include <epix/shader.hpp>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <webgpu/webgpu.hpp>
#endif
#include <epix/render/alpha.hpp>
#include <epix/render/as_bind_group.hpp>
#include <epix/render/assets.hpp>
#include <epix/render/binned_phase.hpp>
#include <epix/render/color_grading.hpp>
#include <epix/render/erased_render_asset.hpp>
#include <epix/render/extract.hpp>
#include <epix/render/fallback_image.hpp>
#include <epix/render/globals.hpp>
#include <epix/render/gpu_readback.hpp>
#include <epix/render/graph.hpp>
#include <epix/render/image.hpp>
#include <epix/render/label.hpp>
#include <epix/render/manual_texture_view.hpp>
#include <epix/render/occlusion_culling.hpp>
#include <epix/render/pipeline.hpp>
#include <epix/render/pipeline_server.hpp>
#include <epix/render/render_phase.hpp>
#include <epix/render/render_debug.hpp>
#include <epix/render/render_resource.hpp>
#include <epix/render/schedule.hpp>
#include <epix/render/specialized_pipeline.hpp>
#include <epix/render/specializer.hpp>
#include <epix/render/storage.hpp>
#include <epix/render/sync_world.hpp>
#include <epix/render/texture_attachment.hpp>
#include <epix/render/view.hpp>
#include <epix/render/visibility_range.hpp>
#include <epix/render/window.hpp>

namespace epix::render {
/**
 * @brief Resource for anonymous surface that is used for requesting adapter/device.
 * Since webgpu requires a surface to request an adapter, we provide this resource to let window implementations to
 * give a functor that creates a surface from the instance. It is recommanded that the functor will destruct the
 * temporary window after we are done with requesting the adapter/device and releasing this resource.
 */
EPIX_EXPORT struct AnonymousSurface {
    std::function<wgpu::Surface(const wgpu::Instance&)> create_surface;
};
/**
 * @brief Priority used when automatically configuring wgpu features/limits
 * (Bevy WgpuSettingsPriority).
 */
EPIX_EXPORT enum class WgpuSettingsPriority {
    Compatibility,
    Functionality,
    WebGL2,
};

/** @brief Reads Bevy's `WGPU_SETTINGS_PRIO` environment setting. Only the
 * case-insensitive `compatibility`, `functionality`, and `webgl2` spellings
 * are recognized; any other value has no effect. */
EPIX_EXPORT std::optional<WgpuSettingsPriority> settings_priority_from_env() noexcept;

/**
 * @brief Renderer configuration for adapter/device creation (Bevy
 * WgpuSettings; the subset relevant to adapter selection). The env vars
 * WGPU_BACKEND / WGPU_POWER_PREF / WGPU_SETTINGS_PRIO are honored by
 * apply_env_overrides like Bevy's settings_priority_from_env.
 */
EPIX_EXPORT struct WgpuSettings {
    /** @brief Optional debug label for the render device. */
    std::optional<std::string> device_label;
    /** @brief Preferred backend. An empty value lets wgpu select from all
     * available backends, matching Bevy's default `Backends::all()`. */
    std::optional<wgpu::BackendType> backends;
    /** @brief Power preference (Bevy default HighPerformance). */
    wgpu::PowerPreference power_preference = wgpu::PowerPreference::eHighPerformance;
    /** @brief Feature/limit priority (Bevy default Functionality). */
    WgpuSettingsPriority priority = WgpuSettingsPriority::Functionality;
    /** @brief If true, prefer a software renderer when available (Bevy
     * force_fallback_adapter). */
    bool force_fallback_adapter = false;
    /** @brief Adapter name filter; when set, the adapter whose info.device
     * matches this substring wins (Bevy adapter_name). The WGPU_ADAPTER_NAME
     * env var takes precedence (Bevy renderer/mod.rs:243-245). */
    std::optional<std::string> adapter_name;
    /** @brief Features to ensure are enabled regardless of adapter support.
     * Mirrors Bevy's default TEXTURE_ADAPTER_SPECIFIC_FORMAT_FEATURES. */
    std::vector<wgpu::FeatureName> features{
        wgpu::FeatureName(wgpu::NativeFeature::eTextureAdapterSpecificFormatFeatures)};
    /** @brief Features to remove from the automatically selected set. */
    std::optional<std::vector<wgpu::FeatureName>> disabled_features;
    /** @brief Imposed device limits. The native wgpu C API uses a null
     * descriptor pointer for its default limits, hence this optional form. */
    std::optional<wgpu::Limits> limits;
    /** @brief Upper/lower bounds applied to the selected adapter limits. */
    std::optional<wgpu::Limits> constrained_limits;
    /** @brief DX12 shader compiler used while creating the wgpu instance. */
    wgpu::Dx12Compiler dx12_shader_compiler = wgpu::Dx12Compiler::eFxc;
    /** @brief Requested GLES 3 minor version for the GL backend. */
    wgpu::Gles3MinorVersion gles3_minor_version = wgpu::Gles3MinorVersion::eAutomatic;
    /** @brief wgpu instance debug/validation flags. */
    wgpu::InstanceFlag instance_flags = wgpu::InstanceFlag::eDefault;

    /** @brief Apply the WGPU_BACKEND / WGPU_POWER_PREF / WGPU_SETTINGS_PRIO
     * environment variables on top of the current values (Bevy
     * settings_priority_from_env / PowerPreference::from_env / Backends
     * from_env). */
    void apply_env_overrides() {
        if (const char* backend = std::getenv("WGPU_BACKEND")) {
            std::string_view b(backend);
            if (b == "vulkan" || b == "Vulkan" || b == "VK")
                backends = wgpu::BackendType::eVulkan;
            else if (b == "dx12" || b == "DX12" || b == "D3D12")
                backends = wgpu::BackendType::eD3D12;
            else if (b == "metal" || b == "METAL")
                backends = wgpu::BackendType::eMetal;
            else if (b == "gl" || b == "GL" || b == "opengl")
                backends = wgpu::BackendType::eOpenGL;
            else if (b == "wgpu" || b == "browser" || b == "WGPU")
                backends = wgpu::BackendType::eWebGPU;
        }
        if (const char* power = std::getenv("WGPU_POWER_PREF")) {
            std::string_view p(power);
            if (p == "low" || p == "Low")
                power_preference = wgpu::PowerPreference::eLowPower;
            else if (p == "high" || p == "High")
                power_preference = wgpu::PowerPreference::eHighPerformance;
        }
        if (auto configured_priority = settings_priority_from_env()) priority = *configured_priority;
    }
};

/** @brief Stable adapter information exposed to render systems (Bevy
 * `RenderAdapterInfo`). Native `wgpu::AdapterInfo` uses borrowed string
 * views, so Epix owns copies of those strings. */
EPIX_EXPORT struct RenderAdapterInfo {
    std::string vendor;
    std::string architecture;
    std::string device;
    std::string description;
    wgpu::BackendType backend_type = wgpu::BackendType::eUndefined;
    wgpu::AdapterType adapter_type = wgpu::AdapterType::eUnknown;
    std::uint32_t vendor_id        = 0;
    std::uint32_t device_id        = 0;

    static RenderAdapterInfo from_adapter(const wgpu::Adapter& adapter);
};

/** @brief Returns the Qualcomm Adreno model number when available (Bevy
 * `get_adreno_model`). Non-Android builds intentionally return nullopt. */
EPIX_EXPORT std::optional<std::uint32_t> get_adreno_model(const RenderAdapterInfo& adapter_info) noexcept;
/** @brief Returns the Mali `v1.rNNp` driver revision when available (Bevy
 * `get_mali_driver_version`). Non-Android builds intentionally return nullopt. */
EPIX_EXPORT std::optional<std::uint32_t> get_mali_driver_version(const RenderAdapterInfo& adapter_info) noexcept;

/** @brief Renderer resources supplied by an embedding application (Bevy
 * `RenderResources`). Epix deliberately exposes the native wgpu handles;
 * their ownership follows the normal wgpu reference-counting rules. */
EPIX_EXPORT struct RenderResources {
    wgpu::Device device;
    wgpu::Queue queue;
    RenderAdapterInfo adapter_info;
    wgpu::Adapter adapter;
    wgpu::Instance instance;
};

/** @brief Chooses whether RenderPlugin creates wgpu resources itself or uses
 * resources supplied by the host (Bevy `RenderCreation`). */
EPIX_EXPORT struct RenderCreation {
    std::variant<WgpuSettings, RenderResources> value{WgpuSettings{}};

    RenderCreation() = default;
    explicit RenderCreation(WgpuSettings settings) : value(std::move(settings)) {}
    explicit RenderCreation(RenderResources resources) : value(std::move(resources)) {}

    static RenderCreation automatic(WgpuSettings settings = {}) { return RenderCreation{std::move(settings)}; }
    static RenderCreation manual(RenderResources resources) { return RenderCreation{std::move(resources)}; }
    static RenderCreation manual(wgpu::Device device,
                                 wgpu::Queue queue,
                                 RenderAdapterInfo adapter_info,
                                 wgpu::Adapter adapter,
                                 wgpu::Instance instance) {
        return manual(RenderResources{std::move(device), std::move(queue), std::move(adapter_info), std::move(adapter),
                                      std::move(instance)});
    }
    bool is_manual() const noexcept { return std::holds_alternative<RenderResources>(value); }
    const WgpuSettings* automatic_settings() const noexcept { return std::get_if<WgpuSettings>(&value); }
    WgpuSettings* automatic_settings() noexcept { return std::get_if<WgpuSettings>(&value); }
    const RenderResources* manual_resources() const noexcept { return std::get_if<RenderResources>(&value); }
    RenderResources* manual_resources() noexcept { return std::get_if<RenderResources>(&value); }
};

/** @brief Plugin that initializes the WebGPU rendering subsystem. */
EPIX_EXPORT struct RenderPlugin {
    /** @brief Validation level (0 = none, 1 = nvrhi, 2 = Vulkan validation
     * layers). */
    int validation = 0;
    /** @brief Automatic or host-provided renderer creation (Bevy
     * `RenderPlugin::render_creation`). */
    RenderCreation render_creation;
    /** @brief Debugging flags (Bevy RenderPlugin::debug_flags). */
    RenderDebugFlags debug_flags;
    /** @brief When true, compile queued pipelines on the render thread rather
     * than the background task pool (Bevy
     * RenderPlugin::synchronous_pipeline_compilation). */
    bool synchronous_pipeline_compilation = true;
    /**
     * @brief Set the validation level for the render plugin.
     * 0 - No validation
     * 1 - Nvrhi validation
     * 2 - Vulkan validation layers
     * @param level the validation level to set
     */
    RenderPlugin& set_validation(int level = 0) noexcept;
    void attach(app::App&);
    void detach(app::App&) noexcept;
};
void render_system(ecs::World& world);
}  // namespace epix::render

// batching.hpp's BatchingPlugin stores RenderDebugFlags by value, so it is
// included after the RenderDebugFlags definition above.
#include <epix/render/batching.hpp>
