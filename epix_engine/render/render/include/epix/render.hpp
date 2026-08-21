#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <cstdlib>
#include <epix/app.hpp>
#include <epix/ecs.hpp>
#include <epix/shader.hpp>
#include <functional>
#include <string_view>
#include <webgpu/webgpu.hpp>
#endif
#include <epix/render/alpha.hpp>
#include <epix/render/as_bind_group.hpp>
#include <epix/render/assets.hpp>
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
#include <epix/render/pipeline.hpp>
#include <epix/render/pipeline_server.hpp>
#include <epix/render/specializer.hpp>
#include <epix/render/binned_phase.hpp>
#include <epix/render/render_phase.hpp>
#include <epix/render/render_resource.hpp>
#include <epix/render/schedule.hpp>
#include <epix/render/specialized_pipeline.hpp>
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

/**
 * @brief Renderer configuration for adapter/device creation (Bevy
 * WgpuSettings; the subset relevant to adapter selection). The env vars
 * WGPU_BACKEND / WGPU_POWER_PREF / WGPU_SETTINGS_PRIO are honored by
 * apply_env_overrides like Bevy's settings_priority_from_env.
 */
EPIX_EXPORT struct WgpuSettings {
    /** @brief Debug label for the render device. */
    std::string device_label = "Render Device";
    /** @brief Preferred backend. Defaults to Vulkan: epix's slang->SPIR-V
     * passthrough path requires it (Bevy defaults to Backends::all(), but the
     * non-passthrough SPIR-V path has open wgpu/naga bugs, so this engine is
     * Vulkan-only). Override via WGPU_BACKEND or the field. */
    std::optional<wgpu::BackendType> backends = wgpu::BackendType::eVulkan;
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
    /** @brief Features to ensure are enabled regardless of what the
     * adapter/backend supports, on top of the engine-mandatory set (Bevy
     * WgpuSettings::features, settings.rs:38-40). */
    std::vector<wgpu::FeatureName> features;
    /** @brief Imposed device limits; when set, passed as the required limits
     * on device creation (Bevy WgpuSettings::limits, settings.rs:44). */
    std::optional<wgpu::Limits> limits;

    /** @brief Apply the WGPU_BACKEND / WGPU_POWER_PREF / WGPU_SETTINGS_PRIO
     * environment variables on top of the current values (Bevy
     * settings_priority_from_env / PowerPreference::from_env / Backends
     * from_env). */
    void apply_env_overrides() {
        if (const char* backend = std::getenv("WGPU_BACKEND")) {
            std::string_view b(backend);
            if (b == "vulkan" || b == "Vulkan" || b == "VK") backends = wgpu::BackendType::eVulkan;
            else if (b == "dx12" || b == "DX12" || b == "D3D12") backends = wgpu::BackendType::eD3D12;
            else if (b == "metal" || b == "METAL") backends = wgpu::BackendType::eMetal;
            else if (b == "gl" || b == "GL" || b == "opengl") backends = wgpu::BackendType::eOpenGL;
            else if (b == "wgpu" || b == "browser" || b == "WGPU") backends = wgpu::BackendType::eWebGPU;
        }
        if (const char* power = std::getenv("WGPU_POWER_PREF")) {
            std::string_view p(power);
            if (p == "low" || p == "Low") power_preference = wgpu::PowerPreference::eLowPower;
            else if (p == "high" || p == "High") power_preference = wgpu::PowerPreference::eHighPerformance;
        }
        if (const char* prio = std::getenv("WGPU_SETTINGS_PRIO")) {
            std::string_view q(prio);
            if (q == "compat" || q == "Compatibility") priority = WgpuSettingsPriority::Compatibility;
            else if (q == "webgl2" || q == "WebGL2") priority = WgpuSettingsPriority::WebGL2;
            else if (q == "func" || q == "Functionality") priority = WgpuSettingsPriority::Functionality;
        }
    }
};

/** @brief Debugging flags that can optionally be set when constructing the
 * renderer (Bevy RenderDebugFlags, lib.rs:133-144). */
EPIX_EXPORT struct RenderDebugFlags {
    /** @brief Raw flag storage (bitflags over u8). */
    std::uint8_t bits = 0;

    constexpr static std::uint8_t ALLOW_COPIES_FROM_INDIRECT_PARAMETERS = 1;

    /** @brief Whether indirect draw parameters get the COPY_SRC flag for CPU
     * readback (Bevy ALLOW_COPIES_FROM_INDIRECT_PARAMETERS). */
    bool allow_copies_from_indirect_parameters() const noexcept {
        return (bits & ALLOW_COPIES_FROM_INDIRECT_PARAMETERS) != 0;
    }
    void set_allow_copies_from_indirect_parameters(bool value = true) noexcept {
        bits = value ? (bits | ALLOW_COPIES_FROM_INDIRECT_PARAMETERS)
                     : (bits & ~ALLOW_COPIES_FROM_INDIRECT_PARAMETERS);
    }
};

/** @brief Plugin that initializes the WebGPU rendering subsystem. */
EPIX_EXPORT struct RenderPlugin {
    /** @brief Validation level (0 = none, 1 = nvrhi, 2 = Vulkan validation
     * layers). */
    int validation = 0;
    /** @brief Renderer configuration (Bevy RenderPlugin::render_creation's
     * WgpuSettings). Defaults mirror Bevy: HighPerformance + Functionality +
     * auto backend; WGPU_BACKEND / WGPU_POWER_PREF / WGPU_SETTINGS_PRIO env
     * vars are honored. */
    WgpuSettings settings;
    /** @brief Debugging flags (Bevy RenderPlugin::debug_flags). */
    RenderDebugFlags debug_flags;
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